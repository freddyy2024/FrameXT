// ProjectController 状态机回归测试（M3 三视角评审修复的防回归钉子）：
// 撤销单步/深度/跨照片、模板切换保留规则、批量应用组撤销、布局过滤。
// controller 是纯 QObject + 值语义，QtCore 环境即可离屏测试。
#include <QtTest>

#include "controllers/ProjectController.h"

#include "metadata/ExifReader.h"
#include "model/PhotoDocument.h"
#include "render/BuiltinTemplates.h"
#include "render/LayoutEngine.h"

using namespace fxt;

namespace {

// 造一张最小文档（elementLayouts 只用 pixelSize + templateState，不读文件）
PhotoDocument makeDoc(const QString &tag, const QSize &px = { 4000, 3000 })
{
    PhotoDocument doc;
    doc.id = QUuid::createUuid();
    doc.sourcePath = QStringLiteral("Z:/nonexistent_%1.jpg").arg(tag);
    doc.meta.pixelSize = px;
    const auto spec = BuiltinTemplates::defaultSpec(QStringLiteral("bottom"));
    doc.templateState.templateId = QStringLiteral("bottom");
    doc.templateState.elements = spec.elements;
    return doc;
}

int customTextCount(const ProjectController &pc)
{
    int n = 0;
    for (const Element &el : pc.current()->templateState.elements) {
        if (el.kind == ElementKind::Text && el.fieldKey.isEmpty())
            ++n;
    }
    return n;
}

const Element &findByField(const PhotoDocument &doc, const QString &key)
{
    for (const Element &el : doc.templateState.elements) {
        if (el.fieldKey == key)
            return el;
    }
    static Element none;
    return none;
}

} // namespace

class TestProjectController : public QObject
{
    Q_OBJECT

private slots:
    // A1：单步撤销回退覆盖层
    void undoSingleStep();
    // A2：删除照片后其撤销快照一并弹出（不复活已删对象）
    void removeCleansUndoStack();
    // A3：撤销栈深上限 20
    void undoDepthCap();
    // A5（钉子）：切模板保留全部自定义文本（QHash 空 key 折叠回归）
    void templateSwitchKeepsAllCustomTexts();
    // A5b（钉子）：绑定字段元素的手动定位跨模板回植
    void templateSwitchReplantsManualPosition();
    // A6：自定义文本上限 5 条
    void addCustomTextLimit();
    // A8：批量应用 Selected + 保留手动位置
    void applySelectedKeepsManualPositions();
    // 数量口径：跳过源照片的净数量（确认框如实告知）
    void applyTargetCountSkipsSource();
    // 批量 = 一个撤销组：undo 一步整组回退
    void applyGroupUndo();
    // A13：清除单字段覆盖恢复原值
    void clearFieldOverride();
    // A14：不可见元素不进画布布局
    void invisibleElementNotInLayouts();

private:
    // 等待防抖渲染静默（不渲染真实图片；非交互测试只关心状态）
    static void drainDebounce() { QTest::qWait(350); }
};

void TestProjectController::undoSingleStep()
{
    ProjectController pc;
    pc.appendDocument(makeDoc("a"));
    drainDebounce();

    pc.setOverride(QStringLiteral("iso"), QStringLiteral("ISO 999"));
    QCOMPARE(pc.current()->overrides.values.value(QStringLiteral("iso")),
             QStringLiteral("ISO 999"));
    QVERIFY(pc.canUndo());
    pc.undo();
    QVERIFY(pc.current()->overrides.isEmpty());
    QVERIFY(!pc.canUndo());
}

void TestProjectController::removeCleansUndoStack()
{
    ProjectController pc;
    const QUuid a = makeDoc("a").id;
    PhotoDocument docA = makeDoc("a");
    docA.id = a;
    pc.appendDocument(docA);
    pc.appendDocument(makeDoc("b"));
    drainDebounce();

    pc.setCurrentId(a);
    pc.setOverride(QStringLiteral("iso"), QStringLiteral("ISO 1"));
    pc.removeDocuments({ a });
    pc.undo();   // 快照已随删除弹出——不应复活已删文档
    QVERIFY(!pc.document(a));
    QCOMPARE(pc.documents().size(), 1);
}

void TestProjectController::undoDepthCap()
{
    ProjectController pc;
    pc.appendDocument(makeDoc("a"));
    drainDebounce();

    for (int i = 0; i < 25; ++i)
        pc.setOverride(QStringLiteral("iso"), QStringLiteral("ISO %1").arg(i));
    int steps = 0;
    while (pc.canUndo()) {
        pc.undo();
        ++steps;
    }
    QCOMPARE(steps, ProjectController::kUndoDepth);
}

void TestProjectController::templateSwitchKeepsAllCustomTexts()
{
    ProjectController pc;
    pc.appendDocument(makeDoc("a"));
    drainDebounce();

    for (int i = 0; i < 3; ++i) {
        const QUuid id = pc.addCustomText();
        QVERIFY(!id.isNull());
    }
    QCOMPARE(customTextCount(pc), 3);

    pc.setTemplateId(QStringLiteral("around"));
    QCOMPARE(customTextCount(pc), 3);   // E9-6：全数保留（空 fieldKey 不折叠）
    pc.setTemplateId(QStringLiteral("bottom"));
    QCOMPARE(customTextCount(pc), 3);
}

void TestProjectController::templateSwitchReplantsManualPosition()
{
    ProjectController pc;
    pc.appendDocument(makeDoc("a"));
    drainDebounce();

    // 找到 paramString 元素手动定位
    QUuid paramId;
    for (const Element &el : pc.current()->templateState.elements) {
        if (el.fieldKey == QLatin1String("paramString"))
            paramId = el.id;
    }
    QVERIFY(!paramId.isNull());
    pc.moveElement(paramId, Anchor::TopLeft, QPointF(0.1, 0.1));
    QVERIFY(findByField(*pc.current(), QStringLiteral("paramString"))
                .hasCustomPosition);

    pc.setTemplateId(QStringLiteral("blur"));
    const Element &param =
        findByField(*pc.current(), QStringLiteral("paramString"));
    QVERIFY(param.hasCustomPosition);   // 回植：位置语义不因切模板丢失
    QCOMPARE(param.anchor, Anchor::TopLeft);
}

void TestProjectController::addCustomTextLimit()
{
    ProjectController pc;
    pc.appendDocument(makeDoc("a"));
    drainDebounce();

    for (int i = 0; i < 5; ++i)
        QVERIFY(!pc.addCustomText().isNull());
    QVERIFY(pc.addCustomText().isNull());   // 第 6 条被拒
    QCOMPARE(customTextCount(pc), 5);
}

void TestProjectController::applySelectedKeepsManualPositions()
{
    ProjectController pc;
    PhotoDocument docA = makeDoc("a");
    const QUuid a = docA.id;
    pc.appendDocument(docA);
    PhotoDocument docB = makeDoc("b");
    const QUuid b = docB.id;
    pc.appendDocument(docB);
    drainDebounce();

    // A 上手动定位 paramString 元素（A 是批量目标，非当前源）
    QUuid aParamId;
    for (const Element &el : pc.document(a)->templateState.elements) {
        if (el.fieldKey == QLatin1String("paramString"))
            aParamId = el.id;
    }
    pc.setCurrentId(a);
    pc.moveElement(aParamId, Anchor::Center, QPointF(0.2, 0.2));

    // 当前 = B（源）；选中集只含 A（不含当前）
    pc.setCurrentId(b);
    pc.setTemplateId(QStringLiteral("blur"));
    pc.setSelection({ a });

    const int applied = pc.applyCurrentTo(ApplyScope::Selected, true);
    QCOMPARE(applied, 1);   // 选中集不含当前 → 当前被捎带，但 A 净作用 1 张

    // A 的手动定位被保留（keepManualPositions）
    const Element &aParam =
        findByField(*pc.document(a), QStringLiteral("paramString"));
    QVERIFY(aParam.hasCustomPosition);
    QCOMPARE(aParam.anchor, Anchor::Center);
    // A 模板被批量改成 blur（自当前照片拷贝）
    QCOMPARE(pc.document(a)->templateState.templateId,
             QStringLiteral("blur"));
}

void TestProjectController::applyTargetCountSkipsSource()
{
    ProjectController pc;
    PhotoDocument docA = makeDoc("a");
    pc.appendDocument(docA);
    pc.appendDocument(makeDoc("b"));
    pc.appendDocument(makeDoc("c"));
    drainDebounce();

    // All = 3 张，跳过当前（源）后净 2——确认框按此展示
    QCOMPARE(pc.applyTargetCount(ApplyScope::All), 2);
    // Current = 只有源自己，净 0（不弹无意义确认）
    QCOMPARE(pc.applyTargetCount(ApplyScope::Current), 0);
}

void TestProjectController::applyGroupUndo()
{
    ProjectController pc;
    PhotoDocument docA = makeDoc("a");
    pc.appendDocument(docA);
    pc.appendDocument(makeDoc("b"));
    pc.appendDocument(makeDoc("c"));
    drainDebounce();

    pc.setTemplateId(QStringLiteral("blur"));
    const int applied = pc.applyCurrentTo(ApplyScope::All, false);
    QCOMPARE(applied, 2);

    pc.undo();   // 一步整组回退：b、c 两张同时回 bottom
    int stillBlur = 0;
    for (const PhotoDocument &doc : pc.documents()) {
        if (doc.templateState.templateId == QLatin1String("blur"))
            ++stillBlur;
    }
    QCOMPARE(stillBlur, 1);   // 只剩源照片 a 自己
    // 组内快照全部弹出（无逐张残余）；栈顶还剩 setTemplateId 的单步快照
    QVERIFY(pc.canUndo());
    pc.undo();
    QCOMPARE(pc.current()->templateState.templateId,
             QStringLiteral("bottom"));   // 源照片回最初模板
    QVERIFY(!pc.canUndo());
}

void TestProjectController::clearFieldOverride()
{
    ProjectController pc;
    pc.appendDocument(makeDoc("a"));
    drainDebounce();

    pc.setOverride(QStringLiteral("iso"), QStringLiteral("ISO 100"));
    pc.clearFieldOverride(QStringLiteral("iso"));
    QVERIFY(pc.current()->overrides.values.value(QStringLiteral("iso"))
                .isEmpty());
    pc.clearFieldOverride(QStringLiteral("iso"));   // 幂等（无覆盖时不入栈）
}

void TestProjectController::invisibleElementNotInLayouts()
{
    ProjectController pc;
    PhotoDocument doc = makeDoc("a");
    doc.sourcePath = QCoreApplication::applicationDirPath()
                     + QStringLiteral("/../../tests/test_projectcontroller.cpp");
    // elementLayouts 不读文件；给元素一个非空 literal 使其参与布局
    doc.templateState.templateId = QStringLiteral("bottom");
    Element custom;
    custom.id = QUuid::createUuid();
    custom.kind = ElementKind::Text;
    custom.literal = QStringLiteral("hello");
    custom.hasCustomPosition = true;
    custom.offset = QPointF(0.03, -0.03);
    doc.templateState.elements.append(custom);
    pc.appendDocument(doc);
    drainDebounce();

    // 可见：在布局里
    bool found = false;
    for (const auto &entry : pc.elementLayouts())
        found |= (entry.first == custom.id);
    QVERIFY(found);

    pc.setElementVisible(custom.id, false);
    found = false;
    for (const auto &entry : pc.elementLayouts())
        found |= (entry.first == custom.id);
    QVERIFY(!found);   // 隐藏元素不可命中（画布交互数据源过滤）
}

QTEST_MAIN(TestProjectController)
#include "test_projectcontroller.moc"
