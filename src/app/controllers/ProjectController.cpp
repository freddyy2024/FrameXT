#include "controllers/ProjectController.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include "metadata/BrandRegistry.h"
#include "metadata/FieldRegistry.h"
#include "render/BuiltinTemplates.h"

namespace fxt {

namespace {
// 预览目标：最长边 ≤2048 下采样（§4.2 预览/导出同管线，scale 差异化）
qreal previewScaleFor(const QSize &original)
{
    const int maxEdge = qMax(original.width(), original.height());
    if (maxEdge <= 0)
        return 1.0;
    return qMin<qreal>(1.0, qreal(2048) / qreal(maxEdge));
}
} // namespace

ProjectController::ProjectController(QObject *parent)
    : QObject(parent)
{
    m_renderDebounce.setSingleShot(true);
    m_renderDebounce.setInterval(250);
    connect(&m_renderDebounce, &QTimer::timeout, this,
            &ProjectController::renderNow);

    m_renderWatcher = new QFutureWatcher<RenderResult>(this);
    connect(m_renderWatcher, &QFutureWatcher<RenderResult>::finished, this,
            [this] {
                const quint64 gen = m_renderGeneration;
                const RenderResult result = m_renderWatcher->result();
                if (gen != m_renderGeneration)
                    return;   // 过期结果（期间又有新请求），丢弃
                if (!result.ok) {
                    if (!result.error.isEmpty())
                        emit renderError(result.error);
                    return;
                }
                m_preview = result.image;
                emit previewUpdated(m_preview);
            });
}

ProjectController::~ProjectController() = default;

PhotoDocument *ProjectController::document(const QUuid &id)
{
    for (PhotoDocument &doc : m_documents)
        if (doc.id == id)
            return &doc;
    return nullptr;
}

PhotoDocument *ProjectController::current()
{
    return document(m_currentId);
}

void ProjectController::setCurrentId(const QUuid &id)
{
    if (m_currentId == id)
        return;
    m_currentId = id;
    emit currentChanged(id);
    renderNow();   // 切照片立即渲染（无防抖）
}

int ProjectController::indexOf(const QUuid &id) const
{
    for (int i = 0; i < m_documents.size(); ++i)
        if (m_documents.at(i).id == id)
            return i;
    return -1;
}

void ProjectController::setSelection(const QSet<QUuid> &ids)
{
    if (m_selection == ids)
        return;
    m_selection = ids;
    emit selectionChanged();
}

void ProjectController::appendDocument(const PhotoDocument &doc)
{
    m_documents.append(doc);
    emit documentAdded(doc.id);
    if (m_currentId.isNull()) {
        setCurrentId(doc.id);   // 首张导入即当前
    }
}

void ProjectController::removeDocuments(const QList<QUuid> &ids)
{
    const QSet<QUuid> doomed(ids.cbegin(), ids.cend());
    // 撤销栈里指向被删照片的快照一并弹出（否则 undo 会复活已删对象的状态）
    m_undoStack.removeIf([this, &doomed](const UndoSnapshot &snap) {
        return doomed.contains(snap.photoId);
    });
    emit undoAvailabilityChanged(canUndo());

    const int oldIndex = indexOf(m_currentId);
    m_documents.removeIf([&doomed](const PhotoDocument &doc) {
        return doomed.contains(doc.id);
    });
    m_selection.subtract(doomed);

    if (doomed.contains(m_currentId)) {
        // 就近补位当前照片（原位次序，保持浏览连续性）
        m_currentId = QUuid();
        if (!m_documents.isEmpty()) {
            const int idx = qMin(oldIndex < 0 ? 0 : oldIndex,
                                 m_documents.size() - 1);
            m_currentId = m_documents.at(qMax(0, idx)).id;
        }
        emit currentChanged(m_currentId);
    }
    for (const QUuid &id : ids)
        emit documentRemoved(id);
    emit selectionChanged();
    renderNow();
}

void ProjectController::clearDocuments()
{
    if (m_documents.isEmpty())
        return;
    m_undoStack.clear();
    emit undoAvailabilityChanged(false);
    m_documents.clear();
    m_selection.clear();
    m_currentId = QUuid();
    emit currentChanged(QUuid());
    emit selectionChanged();
    renderNow();
}

// —— 修改 API ——

void ProjectController::pushUndo(const QUuid &photoId)
{
    PhotoDocument *doc = document(photoId);
    if (!doc)
        return;
    m_undoStack.append({photoId, doc->templateState, doc->overrides});
    if (m_undoStack.size() > kUndoDepth)
        m_undoStack.removeFirst();
    emit undoAvailabilityChanged(true);
}

void ProjectController::setElementVisible(const QUuid &elementId, bool visible)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id == elementId) {
            if (el.visible == visible)
                return;
            pushUndo(doc->id);
            el.visible = visible;
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

void ProjectController::setOverride(const QString &fieldKey, const QString &value)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    if (doc->overrides.values.value(fieldKey) == value)
        return;
    pushUndo(doc->id);
    doc->overrides.values.insert(fieldKey, value);
    emit documentChanged(doc->id);
    scheduleRender();
}

void ProjectController::clearOverrides()
{
    PhotoDocument *doc = current();
    if (!doc || doc->overrides.isEmpty())
        return;
    pushUndo(doc->id);
    doc->overrides.clear();
    emit documentChanged(doc->id);
    scheduleRender();
}

void ProjectController::setTemplateId(const QString &templateId)
{
    PhotoDocument *doc = current();
    if (!doc || doc->templateState.templateId == templateId)
        return;
    pushUndo(doc->id);
    // 模板切换：元素树/行组重建（E9-6：自定义文本=kind Free 保留；
    // 手动定位元素保留位置语义——按 fieldKey 匹配回植 hasCustomPosition）
    QHash<QString, Element> previous;
    for (const Element &el : doc->templateState.elements)
        previous.insert(el.fieldKey, el);

    const BuiltinTemplates::BuiltinSpec spec =
        BuiltinTemplates::defaultSpec(templateId);
    doc->templateState.templateId = templateId;
    doc->templateState.templateParams.clear();

    // 自定义文本（fieldKey 空 = 自由文本）跨模板保留（E9-6）
    QList<Element> kept;
    for (const Element &el : previous) {
        if (el.kind == ElementKind::Text && el.fieldKey.isEmpty())
            kept.append(el);
    }
    doc->templateState.elements = spec.elements + kept;
    emit documentChanged(doc->id);
    scheduleRender();
}

void ProjectController::moveElement(const QUuid &elementId, Anchor anchor,
                                    const QPointF &offset)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id == elementId) {
            pushUndo(doc->id);
            el.anchor = anchor;
            el.offset = offset;
            el.hasCustomPosition = true;
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

void ProjectController::setElementStyle(const QUuid &elementId,
                                        const TextStyle &style)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id == elementId) {
            if (!(el.style == style)) {
                pushUndo(doc->id);
                el.style = style;
                emit documentChanged(doc->id);
                scheduleRender();
            }
            return;
        }
    }
}

void ProjectController::raiseElement(const QUuid &elementId)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    QList<Element> &elements = doc->templateState.elements;
    for (int i = 0; i < elements.size(); ++i) {
        if (elements.at(i).id == elementId) {
            if (i == 0)
                return;
            pushUndo(doc->id);
            const Element el = elements.takeAt(i);
            elements.prepend(el);
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

void ProjectController::resetElementPosition(const QUuid &elementId)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id == elementId && el.hasCustomPosition) {
            pushUndo(doc->id);
            el.hasCustomPosition = false;
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

// —— 撤销 ——

void ProjectController::undo()
{
    if (m_undoStack.isEmpty())
        return;
    const UndoSnapshot snap = m_undoStack.takeLast();
    PhotoDocument *doc = document(snap.photoId);
    if (doc) {
        doc->templateState = snap.state;
        doc->overrides = snap.overrides;
        emit documentChanged(doc->id);
        if (doc->id == m_currentId)
            renderNow();
    }
    emit undoAvailabilityChanged(canUndo());
}

// —— 批量应用 ——

QList<QUuid> ProjectController::scopeTargets(ApplyScope scope) const
{
    switch (scope) {
    case ApplyScope::Current:
        return m_currentId.isNull() ? QList<QUuid>() : QList<QUuid>{m_currentId};
    case ApplyScope::Selected: {
        QList<QUuid> ids = m_selection.values();
        if (m_selection.contains(m_currentId))
            return ids;
        ids.prepend(m_currentId);   // 选中集不含当前时，当前也一并应用
        return ids;
    }
    case ApplyScope::All: {
        QList<QUuid> ids;
        for (const PhotoDocument &doc : m_documents)
            ids.append(doc.id);
        return ids;
    }
    }
    return {};
}

int ProjectController::applyCurrentTo(ApplyScope scope, bool keepManualPositions)
{
    PhotoDocument *src = current();
    if (!src)
        return 0;

    // 一次批量 = 一个撤销快照组（逐张 push 前像；undo 逐步回退可接受）
    int applied = 0;
    for (const QUuid &id : scopeTargets(scope)) {
        PhotoDocument *doc = document(id);
        if (!doc || doc->id == src->id)
            continue;
        pushUndo(doc->id);
        TemplateState next = src->templateState;
        if (keepManualPositions) {
            // 保留该照片的手动定位元素（按 fieldKey 匹配回植）
            for (const Element &old : doc->templateState.elements) {
                if (!old.hasCustomPosition)
                    continue;
                for (Element &el : next.elements) {
                    if (el.fieldKey == old.fieldKey && !el.fieldKey.isEmpty()) {
                        el.anchor = old.anchor;
                        el.offset = old.offset;
                        el.hasCustomPosition = true;
                        break;
                    }
                }
            }
        }
        doc->templateState = next;
        emit documentChanged(doc->id);
        ++applied;
    }
    if (applied > 0)
        renderNow();
    return applied;
}

// —— 渲染 ——

RenderRequest ProjectController::renderRequest(const PhotoDocument &doc) const
{
    RenderRequest req;
    req.photoId = doc.id;
    req.filePath = doc.sourcePath;
    req.templateState = doc.templateState;
    req.overrides = doc.overrides;
    req.scale = previewScaleFor(doc.meta.pixelSize);
    req.purpose = RenderPurpose::Preview;
    return req;
}

void ProjectController::requestPreview()
{
    m_renderDebounce.start();   // 250ms 防抖合并连续修改（§4.2）
}

void ProjectController::scheduleRender()
{
    m_renderDebounce.start();   // 修改 API 的防抖入口（与 requestPreview 同径）
}

void ProjectController::renderNow()
{
    m_renderDebounce.stop();
    PhotoDocument *doc = current();
    if (!doc) {
        m_preview = QImage();
        emit previewUpdated(m_preview);
        return;
    }
    const RenderRequest req = renderRequest(*doc);
    const auto resolver = fieldResolver(*doc);
    const QImage logo = resolveLogo(*doc);
    const bool hideWatermark = m_watermarkHidden;
    const quint64 gen = ++m_renderGeneration;

    TemplateState state = req.templateState;
    if (hideWatermark) {
        for (Element &el : state.elements)
            el.visible = false;   // 纯看图：渲染层面跳过所有元素
    }

    RenderCacheManager *cache = nullptr;   // nullptr = 进程级共享缓存（§4.2）
    QFuture<RenderResult> future = QtConcurrent::run(
        [req, state, resolver, logo, cache]() {
            RenderRequest r = req;
            r.templateState = state;
            return RenderPipeline::run(r, resolver,
                                       [logo](const QString &) { return logo; },
                                       cache);
        });
    m_renderWatcher->setFuture(future);
}

void ProjectController::setWatermarkHidden(bool hidden)
{
    if (m_watermarkHidden == hidden)
        return;
    m_watermarkHidden = hidden;
    renderNow();
}

FieldValueResolver ProjectController::fieldResolver(const PhotoDocument &doc) const
{
    // 覆盖层优先（resolveDisplay 语义）；fields 为导入时算好的展示值集
    const QHash<QString, QString> base = doc.meta.exif.fields;
    const QHash<QString, QString> ov = doc.overrides.values;
    return [base, ov](const QString &fieldKey) -> QString {
        if (fieldKey == QLatin1String("paramString")) {
            // 参数串：焦距/等效/光圈/快门/ISO 组合（§7.2 经典格式）
            QStringList parts;
            const auto append = [&parts, &base, &ov](const char *key) {
                const QString v = ov.value(QLatin1String(key),
                                           base.value(QLatin1String(key)));
                if (!v.isEmpty())
                    parts.append(v);
            };
            append("focalLength");
            append("focalLengthEq");
            append("fNumber");
            append("shutter");
            append("iso");
            return parts.join(QLatin1Char(' '));
        }
        return ov.value(fieldKey, base.value(fieldKey));
    };
}

QImage ProjectController::resolveLogo(const PhotoDocument &doc) const
{
    const BrandDef brand =
        BrandRegistry::match(doc.meta.exif.make, doc.meta.exif.model);
    if (brand.value == QLatin1String("Others"))
        return QImage();   // 未识别品牌不出 Logo（宁缺勿错）
    return QImage(brand.logoPath);
}

} // namespace fxt
