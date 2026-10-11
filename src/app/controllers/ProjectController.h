#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUuid>

#include "model/PhotoDocument.h"
#include "render/LayoutEngine.h"
#include "render/RenderPipeline.h"

namespace fxt {

// 应用范围（模板/状态批量应用，04-UI §5.5）
enum class ApplyScope { Current, Selected, All };

struct UndoSnapshot
{
    QUuid photoId;
    TemplateState state;        // 修改前快照
    ExifOverrides overrides;
    QUuid groupId;              // 批量操作组（一次 undo 整组回退；空 = 单步）
};

// 项目状态中枢（02-技术设计 §4.1/§4.2 controller 层）：
// - 文档列表/当前照片/选中集（双层选择：当前=编辑对象，选中=批量对象）
// - 快照式撤销（栈深 20，每步=单张照片的 TemplateState+Overrides 前像）
// - 预览渲染调度（250ms 防抖 + QtConcurrent 后台执行 + 代号过期丢弃）
// - FieldValueResolver / LogoResolver 装配（渲染管线接 M2 元数据引擎）
class ProjectController : public QObject
{
    Q_OBJECT

public:
    explicit ProjectController(QObject *parent = nullptr);
    ~ProjectController() override;

    // —— 文档集 ——
    const QList<PhotoDocument> &documents() const { return m_documents; }
    PhotoDocument *document(const QUuid &id);
    PhotoDocument *current();
    const PhotoDocument *current() const;
    QUuid currentId() const { return m_currentId; }
    void setCurrentId(const QUuid &id);
    int indexOf(const QUuid &id) const;

    // 选中集（照片列表多选；空 = 无批量选择）
    QSet<QUuid> selection() const { return m_selection; }
    void setSelection(const QSet<QUuid> &ids);

    // 导入结果落库（ImportController 回调主线程后调用）
    void appendDocument(const PhotoDocument &doc);
    void removeDocuments(const QList<QUuid> &ids);
    void clearDocuments();

    // —— 修改 API（自动撤销快照 + 防抖渲染）——
    void setElementVisible(const QUuid &elementId, bool visible);
    void setOverride(const QString &fieldKey, const QString &value);
    void clearFieldOverride(const QString &fieldKey);   // 恢复该字段原值
    void clearOverrides();
    void setTemplateId(const QString &templateId);
    // 模板参数微调（M5.1，滑杆/管理器）：写入 TemplateState::templateParams
    // 并防抖重渲染。不进撤销栈——滑杆连续拖动会刷穿 20 深快照栈（与 M3
    // 挂账「撤销粒度合并」同类问题），撤销粒度待挂账统一解决
    void setTemplateParam(const QString &key, double value);
    // 手动定位：锚点 + 相对偏移；hasCustomPosition=true 脱离行组
    void moveElement(const QUuid &elementId, Anchor anchor, const QPointF &offset);
    // 拖动增量（画布松手提交；保持原锚点，offset 累加）
    void nudgeElement(const QUuid &elementId, const QPointF &offsetDelta);
    // 元素样式批量改（选中元素样式区）
    void setElementStyle(const QUuid &elementId, const TextStyle &style);
    // 自由文本内容改（fieldKey 为空的元素；绑定字段元素走 setOverride）
    void setElementLiteral(const QUuid &elementId, const QString &text);
    // 真删除元素（仅自定义文本；模板元素用 setElementVisible 隐藏）
    void removeElement(const QUuid &elementId);
    // 新增自定义文本（≤5，04-UI §3.5），返回新元素 id（超限返回空）
    QUuid addCustomText();
    // 置顶（行组重排为第一行首位；脱离行组则改 zIndex 语义=移到列表头）
    void raiseElement(const QUuid &elementId);
    // 还原位置（回到行组自动排布）
    void resetElementPosition(const QUuid &elementId);

    // —— 撤销 ——
    void undo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }

    // —— 批量应用（§5.5：当前模板+水印状态 → 范围；可撤销）——
    // keepManualPositions：保留各照片 hasCustomPosition 的手动定位；
    // 一次批量 = 一个撤销组，undo 一步整组回退
    int applyCurrentTo(ApplyScope scope, bool keepManualPositions);
    // 确认框如实展示作用面（跳过源照片后的净数量）
    int applyTargetCount(ApplyScope scope) const;

    // —— 渲染装配 ——
    RenderRequest renderRequest(const PhotoDocument &doc) const;
    void requestPreview();       // 防抖后渲染当前照片
    void renderNow();            // 立即渲染（切照片等无防抖场景）
    QImage lastPreview() const { return m_preview; }

    // 布局查询（画布命中/拖动/就地编辑用）：与渲染 S5 同源解算，
    // 列表序 = 元素 z 序（前 = 上层）。纯看图态仍返回完整布局。
    QList<QPair<QUuid, ElementLayout>> elementLayouts();

    // 水印显隐（纯看图模式，渲染时 visible 元素整体跳过）
    bool watermarkHidden() const { return m_watermarkHidden; }
    void setWatermarkHidden(bool hidden);
    void toggleWatermarkHidden();

    // resolver 装配（渲染管线 + 画布元素信息）
    FieldValueResolver fieldResolver(const PhotoDocument &doc) const;
    QImage resolveLogo(const PhotoDocument &doc) const;

    static constexpr int kUndoDepth = 20;

signals:
    void documentAdded(const QUuid &id);
    void documentRemoved(const QUuid &id);
    void currentChanged(const QUuid &id);
    void selectionChanged();
    void documentChanged(const QUuid &id);       // 状态变更（面板/列表刷新）
    void previewUpdated(const QImage &image);
    void undoAvailabilityChanged(bool available);
    void watermarkHiddenChanged(bool hidden);
    void renderError(const QString &message);

private:
    void pushUndo(const QUuid &photoId);
    void pushUndo(const QUuid &photoId, const QUuid &groupId);
    void scheduleRender();
    QList<QUuid> scopeTargets(ApplyScope scope) const;
    CanvasPlan canvasPlanFor(const PhotoDocument &doc) const;   // 与渲染 S2 同源

    QList<PhotoDocument> m_documents;
    QUuid m_currentId;
    QSet<QUuid> m_selection;
    QList<UndoSnapshot> m_undoStack;
    bool m_watermarkHidden = false;

    QImage m_preview;
    QTimer m_renderDebounce;       // 250ms 防抖（§4.2）
    QFutureWatcher<RenderResult> *m_renderWatcher = nullptr;
    quint64 m_renderGeneration = 0;  // 过期渲染结果丢弃
    quint64 m_watcherGeneration = 0; // watcher 当前所挂请求的代号
};

} // namespace fxt
