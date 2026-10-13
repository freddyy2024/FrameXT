#include "controllers/ProjectController.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include "metadata/BrandRegistry.h"
#include "metadata/FieldRegistry.h"
#include "model/TemplateSpec.h"
#include "render/BuiltinTemplates.h"
#include "render/CanvasGeometry.h"

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
                // 空 future 守卫（必须在最前）：无图分支 setFuture(空 QFuture)
                // 作废在途结果——空 future 本身即 finished 态，watcher 照常发
                // finished，此时 result() 会在空结果存储上解引用迭代器
                // （MSVC Debug 断言：cannot dereference value-initialized
                // map/set iterator，无图时切水印开关必现）
                if (m_renderWatcher->future().resultCount() < 1)
                    return;
                const quint64 gen = m_watcherGeneration;
                const RenderResult result = m_renderWatcher->result();
                if (gen != m_renderGeneration)
                    return;   // 过期结果（请求时代号已被 ++），丢弃
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

const PhotoDocument *ProjectController::current() const
{
    for (const PhotoDocument &doc : m_documents) {
        if (doc.id == m_currentId)
            return &doc;
    }
    return nullptr;
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
    pushUndo(photoId, QUuid());
}

void ProjectController::pushUndo(const QUuid &photoId, const QUuid &groupId)
{
    PhotoDocument *doc = document(photoId);
    if (!doc)
        return;
    m_undoStack.append({photoId, doc->templateState, doc->overrides, groupId});
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

void ProjectController::clearFieldOverride(const QString &fieldKey)
{
    PhotoDocument *doc = current();
    if (!doc || !doc->overrides.values.contains(fieldKey))
        return;
    pushUndo(doc->id);
    doc->overrides.values.remove(fieldKey);
    emit documentChanged(doc->id);
    scheduleRender();
}

void ProjectController::setTemplateId(const QString &templateId)
{
    PhotoDocument *doc = current();
    if (!doc || doc->templateState.templateId == templateId)
        return;
    pushUndo(doc->id);
    // 模板切换：元素树/行组重建。
    // 保留规则（E9-6）：① 自定义文本（fieldKey 空）全数保留——不能用
    // fieldKey 做 key 收集（空 key 在 QHash 里互相覆盖）；
    // ② 绑定字段元素的手动定位按 fieldKey 回植到新元素树
    QList<Element> keptCustom;
    QHash<QString, Element> positioned;
    for (const Element &el : doc->templateState.elements) {
        if (el.kind == ElementKind::Text && el.fieldKey.isEmpty())
            keptCustom.append(el);
        else if (el.hasCustomPosition && !el.fieldKey.isEmpty())
            positioned.insert(el.fieldKey, el);
    }

    const BuiltinTemplates::BuiltinSpec spec =
        BuiltinTemplates::defaultSpec(
            TemplateCatalog::specFor(templateId).kind);
    doc->templateState.templateId = templateId;
    // 参数初值 = 模板声明默认（用户模板即其保存的参数集；内置模板 = 视觉常量）
    doc->templateState.templateParams =
        TemplateCatalog::specFor(templateId).defaultParams();

    QList<Element> merged = spec.elements;
    for (Element &el : merged) {
        const auto it = positioned.constFind(el.fieldKey);
        if (it != positioned.constEnd()) {
            el.anchor = it->anchor;
            el.offset = it->offset;
            el.hasCustomPosition = true;
        }
    }
    doc->templateState.elements = merged + keptCustom;
    emit documentChanged(doc->id);
    scheduleRender();
}

void ProjectController::setTemplateParam(const QString &key, double value)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    doc->templateState.templateParams.insert(key, value);
    // 只影响渲染几何/模糊（不动元素树），documentChanged 的面板全刷
    // 没有必要；防抖渲染足够，UI 滑杆自身是值来源无需回填
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

void ProjectController::nudgeElement(const QUuid &elementId,
                                     const QPointF &offsetDelta)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id != elementId)
            continue;
        QPointF startOffset = el.offset;
        if (!el.hasCustomPosition) {
            // 首次拖动：行组排布位置与 anchor+offset 参考系不同源，
            // 先把当前像素位置物化为 offset 基准，否则元素会跳变到锚点附近
            const CanvasPlan plan = canvasPlanFor(*doc);
            if (!plan.canvasSize.isEmpty()) {
                const auto layouts = LayoutEngine::resolve(
                    doc->templateState.elements,
                    BuiltinTemplates::defaultRows(doc->templateState.elements),
                    plan, fieldResolver(*doc),
                    [this, doc](const QString &) { return resolveLogo(*doc); });
                const auto it = layouts.constFind(elementId);
                if (it != layouts.constEnd()) {
                    const QPointF base =
                        LayoutEngine::anchorBase(el.anchor, plan);
                    startOffset =
                        QPointF((it->pxRect.left() - base.x())
                                    / plan.canvasSize.width(),
                                (it->pxRect.top() - base.y())
                                    / plan.canvasSize.height());
                }
            }
        }
        pushUndo(doc->id);
        el.offset = startOffset + offsetDelta;
        el.hasCustomPosition = true;
        emit documentChanged(doc->id);
        scheduleRender();
        return;
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

void ProjectController::setElementLiteral(const QUuid &elementId,
                                          const QString &text)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id == elementId && el.fieldKey.isEmpty()) {
            if (el.literal == text)
                return;
            pushUndo(doc->id);
            el.literal = text;
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

void ProjectController::setElementCustomLogo(const QUuid &elementId,
                                             const QString &path)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    for (Element &el : doc->templateState.elements) {
        if (el.id == elementId && el.kind == ElementKind::Logo) {
            if (el.customLogoPath == path)
                return;
            pushUndo(doc->id);
            el.customLogoPath = path;   // 空 = 还原品牌自动匹配
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

void ProjectController::removeElement(const QUuid &elementId)
{
    PhotoDocument *doc = current();
    if (!doc)
        return;
    QList<Element> &elements = doc->templateState.elements;
    for (int i = 0; i < elements.size(); ++i) {
        if (elements.at(i).id == elementId) {
            pushUndo(doc->id);
            elements.removeAt(i);
            emit documentChanged(doc->id);
            scheduleRender();
            return;
        }
    }
}

QUuid ProjectController::addCustomText()
{
    PhotoDocument *doc = current();
    if (!doc)
        return {};
    // 上限 5 条自定义文本（04-UI §3.5）
    int freeCount = 0;
    for (const Element &el : doc->templateState.elements) {
        if (el.kind == ElementKind::Text && el.fieldKey.isEmpty())
            ++freeCount;
    }
    if (freeCount >= 5)
        return {};

    pushUndo(doc->id);
    Element el;
    el.id = QUuid::createUuid();
    el.kind = ElementKind::Text;
    el.fieldKey.clear();
    el.literal = tr("自定义文本");
    el.anchor = Anchor::BottomLeft;
    el.offset = QPointF(0.03, -0.03);   // 左下角内缩（视觉默认位）
    el.hasCustomPosition = true;        // 自定义文本天然脱离行组
    doc->templateState.elements.append(el);
    emit documentChanged(doc->id);
    scheduleRender();
    return el.id;
}

// —— 撤销 ——

void ProjectController::undo()
{
    if (m_undoStack.isEmpty())
        return;
    // 批量操作（同一 groupId）整组回退 = 一步撤销（3.7 准入）；
    // 空组号的普通操作只弹一个快照
    const QUuid group = m_undoStack.last().groupId;
    bool restored = false;
    QUuid lastRestoredId;
    while (!m_undoStack.isEmpty()) {
        if (!group.isNull()
            && m_undoStack.last().groupId != group)
            break;   // 组边界：整组弹完即止
        const UndoSnapshot snap = m_undoStack.takeLast();
        PhotoDocument *doc = document(snap.photoId);
        if (doc) {
            doc->templateState = snap.state;
            doc->overrides = snap.overrides;
            emit documentChanged(doc->id);
            lastRestoredId = doc->id;
            restored = true;
        }
        if (group.isNull())
            break;   // 单步：弹一个即止
    }
    if (restored) {
        // 批量回退涉及多张时跳到最近回退的对象，让用户看到撤销效果
        if (lastRestoredId != m_currentId)
            setCurrentId(lastRestoredId);
        else
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

    // 一次批量 = 一个撤销组：undo 一步整组回退（3.7 准入）
    const QUuid groupId = QUuid::createUuid();
    int applied = 0;
    for (const QUuid &id : scopeTargets(scope)) {
        PhotoDocument *doc = document(id);
        if (!doc || doc->id == src->id)
            continue;
        pushUndo(doc->id, groupId);
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

int ProjectController::applyTargetCount(ApplyScope scope) const
{
    // 确认框如实展示作用面（含源照片跳过后的净数量）
    const PhotoDocument *src = current();
    int count = 0;
    for (const QUuid &id : scopeTargets(scope)) {
        if (src && id == src->id)
            continue;
        ++count;
    }
    return count;
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

CanvasPlan ProjectController::canvasPlanFor(const PhotoDocument &doc) const
{
    // 与 RenderPipeline S2 同源：预览缩放尺寸 → 画布 plan（模板参数同步）
    const qreal scale = previewScaleFor(doc.meta.pixelSize);
    const QSize scaled(qRound(doc.meta.pixelSize.width() * scale),
                       qRound(doc.meta.pixelSize.height() * scale));
    return CanvasGeometry::plan(doc.templateState.templateId, scaled,
                                doc.templateState.templateParams);
}

QList<QPair<QUuid, ElementLayout>> ProjectController::elementLayouts()
{
    PhotoDocument *doc = current();
    if (!doc || doc->meta.pixelSize.isEmpty())
        return {};
    const CanvasPlan plan = canvasPlanFor(*doc);
    if (plan.canvasSize.isEmpty())
        return {};

    const auto resolver = fieldResolver(*doc);
    const QHash<QUuid, ElementLayout> resolved = LayoutEngine::resolve(
        doc->templateState.elements,
        BuiltinTemplates::defaultRows(doc->templateState.elements), plan,
        resolver,
        [this, doc](const QString &key) {
            Q_UNUSED(key);
            return resolveLogo(*doc);
        });

    // 按元素列表序输出（前 = 上层，画布命中测试从上层往下）
    QList<QPair<QUuid, ElementLayout>> ordered;
    for (const Element &el : doc->templateState.elements) {
        if (!el.visible)
            continue;   // 不可见元素不参与画布命中
        const auto it = resolved.constFind(el.id);
        if (it == resolved.constEnd())
            continue;
        ElementLayout layout = it.value();
        // 就地编辑初值必须是未截断文本（LayoutEngine 行溢出时
        // resolvedText 已被 elide，写回会静默破坏原值）
        layout.resolvedText =
            el.fieldKey.isEmpty() ? el.literal : resolver(el.fieldKey);
        ordered.append({ el.id, layout });
    }
    return ordered;
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
    // 使在途旧结果过期（清空列表等空分支同样要作废幽灵预览）
    const quint64 gen = ++m_renderGeneration;
    if (!doc) {
        m_renderWatcher->setFuture(QFuture<RenderResult>());
        m_preview = QImage();
        emit previewUpdated(m_preview);
        return;
    }
    const RenderRequest req = renderRequest(*doc);
    const auto resolver = fieldResolver(*doc);
    const QImage logo = resolveLogo(*doc);
    const bool hideWatermark = m_watermarkHidden;
    m_watcherGeneration = gen;

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
    emit watermarkHiddenChanged(hidden);
    renderNow();
}

void ProjectController::toggleWatermarkHidden()
{
    setWatermarkHidden(!m_watermarkHidden);
}

FieldValueResolver ProjectController::fieldResolver(const PhotoDocument &doc) const
{
    // 覆盖层优先（resolveDisplay 语义）；fields 为导入时算好的展示值集
    const QHash<QString, QString> base = doc.meta.exif.fields;
    const QHash<QString, QString> ov = doc.overrides.values;
    return [base, ov](const QString &fieldKey) -> QString {
        if (fieldKey == QLatin1String("paramString")) {
            // 参数串：焦距/光圈/快门/ISO 组合（§7.2 经典格式）。
            // 等效焦距不入串（用户指令 2026-10-14：与原版视觉不符且冗长），
            // 仍作为独立字段 focalLengthEq 可单独启用
            QStringList parts;
            const auto append = [&parts, &base, &ov](const char *key) {
                const QString v = ov.value(QLatin1String(key),
                                           base.value(QLatin1String(key)));
                if (!v.isEmpty())
                    parts.append(v);
            };
            append("focalLength");
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
