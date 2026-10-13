// S5 LayoutEngine / S6 ElementPainter 单测（03-开发计划 1.3/1.4）：
// 行组收紧、手动定位优先、Logo 等比、分隔线比例、文本绘制冒烟
#include <QImage>
#include <QPainter>
#include <QtTest>

#include "model/Element.h"
#include "render/BackdropRenderer.h"
#include "render/CanvasGeometry.h"
#include "render/ElementPainter.h"
#include "render/LayoutEngine.h"

using namespace fxt;
using namespace fxt::BuiltinTemplate;

namespace {

struct Fixture
{
    CanvasPlan plan;
    QList<Element> elements;
    QHash<QString, QString> fields;
    QHash<QString, QImage> logos;

    static Fixture make()
    {
        Fixture f;
        f.plan = CanvasGeometry::plan(QLatin1String(kBottom), {1000, 750});
        // band = (25, 751, 950, 90)（左右各 2.5%W 内边距）；
        // 两行 0.6/0.4 → row0: y=751 h=54, row1: y=805 h=36

        Element brand;
        brand.id = QUuid::createUuid();
        brand.kind = ElementKind::Text;
        // 默认模板左块第一行 = 机型（2026-10 布局改版，原 make 元素退役）
        brand.fieldKey = QStringLiteral("model");
        f.elements.append(brand);

        Element logo;
        logo.id = QUuid::createUuid();
        logo.kind = ElementKind::Logo;
        logo.fieldKey = QStringLiteral("brandLogo");
        logo.logoScale = 0.5;
        f.elements.append(logo);

        Element divider;
        divider.id = QUuid::createUuid();
        divider.kind = ElementKind::Divider;
        f.elements.append(divider);

        Element params;
        params.id = QUuid::createUuid();
        params.kind = ElementKind::Text;
        params.fieldKey = QStringLiteral("paramString");
        f.elements.append(params);

        Element lens;
        lens.id = QUuid::createUuid();
        lens.kind = ElementKind::Text;
        lens.fieldKey = QStringLiteral("lens");
        f.elements.append(lens);

        f.fields.insert("model", "Canon");
        f.fields.insert("paramString", "42mm · f/5.6");
        f.fields.insert("lens", "RF 24-70mm");

        // 2:1 测试 Logo
        f.logos.insert("brandLogo", QImage(200, 100, QImage::Format_ARGB32));
        return f;
    }

    FieldValueResolver resolver() const
    {
        return [this](const QString &key) { return fields.value(key); };
    }

    LogoResolver logoResolver() const
    {
        return [this](const QString &key) { return logos.value(key); };
    }

    LayoutRow makeRow(int index, qreal ratio, const QList<QUuid> &ids) const
    {
        LayoutRow row;
        row.index = index;
        row.heightRatio = ratio;
        row.elementIds = ids;
        return row;
    }
};

} // namespace

class TestLayout : public QObject
{
    Q_OBJECT

private slots:
    void rowPlacement() const;
    void tightenSkipsInvisible() const;
    void tightenSkipsEmptyField() const;
    void literalOverridesField() const;
    void emptyRowFreesSpaceForNext() const;
    void missingLogoDroppedFromRow() const;
    void rowOverflowShrinksFont() const;
    void customPositionOverridesRow() const;
    void logoAspectRatio() const;
    void dividerProportions() const;
    void paintTextRendersPixels() const;
    void singleLineModelBoost() const;
};

void TestLayout::rowPlacement() const
{
    Fixture f = Fixture::make();
    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id, f.elements[1].id,
                                               f.elements[2].id, f.elements[3].id });
    const LayoutRow row1 = f.makeRow(1, 0.4, { f.elements[4].id });

    const auto layouts = LayoutEngine::resolve(f.elements, { row0, row1 }, f.plan,
                                               f.resolver(), f.logoResolver());
    QCOMPARE(layouts.size(), 5);

    // 紧凑行距（2026-10-14 用户指令）：两行按内容高成组、带内整体居中。
    // 本例两行内容等高（元素默认样式）→ 上下留白对称（±2 容差吸收度量取整）
    const QRectF brandRect = layouts.value(f.elements[0].id).pxRect;
    const QRectF lensRect = layouts.value(f.elements[4].id).pxRect;
    const qreal bandTop = f.plan.photoRect.bottom() + 1.0;
    const qreal bandBottom = bandTop + f.plan.exBandHeight;
    QVERIFY2(qAbs((brandRect.top() - bandTop) - (bandBottom - lensRect.bottom()))
                 <= 2.0,
             "紧凑组未在信息带内垂直居中");
    // 行间隙紧凑：≤ 带高 12%（kRowGapRatio 8% + 度量余量）
    QVERIFY2(lensRect.top() - brandRect.bottom()
                 <= f.plan.exBandHeight * 0.12,
             "行间距超出紧凑档");
    QVERIFY(lensRect.top() > brandRect.bottom());
    // 行内水平排布：brand 在 logo 左侧
    QVERIFY(brandRect.right() < layouts.value(f.elements[1].id).pxRect.left());
}

void TestLayout::tightenSkipsInvisible() const
{
    Fixture f = Fixture::make();
    f.elements[1].visible = false;   // Logo 隐藏
    f.elements[2].visible = false;   // 分隔线隐藏

    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id, f.elements[1].id,
                                               f.elements[2].id, f.elements[3].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());

    // 收紧后只排 brand 与 params；brand 从行左端（含 2.5%W 信息带内边距）开始
    QCOMPARE(layouts.size(), 2);
    const QRectF brandRect = layouts.value(f.elements[0].id).pxRect;
    const QRectF paramRect = layouts.value(f.elements[3].id).pxRect;
    QCOMPARE(brandRect.left(), f.plan.photoRect.width() * 0.025);
    QVERIFY(paramRect.left() > brandRect.right());
}

void TestLayout::tightenSkipsEmptyField() const
{
    Fixture f = Fixture::make();
    f.fields.remove("paramString");   // 参数串字段缺失

    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id, f.elements[3].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());
    QCOMPARE(layouts.size(), 1);   // 仅剩 brand
    QVERIFY(!layouts.contains(f.elements[3].id));
}

void TestLayout::literalOverridesField() const
{
    // literal（自由文本/覆盖值）优先于字段解析值
    Fixture f = Fixture::make();
    f.elements[0].literal = QStringLiteral("OVERRIDE");

    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());
    QCOMPARE(layouts.size(), 1);
    QCOMPARE(layouts.value(f.elements[0].id).resolvedText, QStringLiteral("OVERRIDE"));
}

void TestLayout::emptyRowFreesSpaceForNext() const
{
    // 行内全部被剔除（Logo 缺图）→ 整行不占位，下一行前移到信息带顶
    Fixture f = Fixture::make();
    f.logos.clear();   // brandLogo 缺图

    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[1].id });   // 仅 Logo
    const LayoutRow row1 = f.makeRow(1, 0.4, { f.elements[4].id });   // lens
    const auto layouts = LayoutEngine::resolve(f.elements, { row0, row1 }, f.plan,
                                               f.resolver(), f.logoResolver());
    QVERIFY(!layouts.contains(f.elements[1].id));   // 缺图 Logo 剔除
    // 行0 剔除后 lens 独占信息带：紧凑布局下带内垂直居中（中心 ≈ 带中线）
    //（未前移则明显偏带底）
    const QRectF lensRect = layouts.value(f.elements[4].id).pxRect;
    const qreal bandTop = f.plan.photoRect.bottom() + 1.0;
    QVERIFY2(qAbs(lensRect.center().y() - (bandTop + f.plan.exBandHeight / 2.0))
                 <= 1.0,
             "空行未释放空间：lens 未在信息带内居中");
}

void TestLayout::missingLogoDroppedFromRow() const
{
    // Logo 缺图 → 行内不留占位空洞：params 左移到 brand + gap 处
    Fixture f = Fixture::make();
    f.logos.clear();

    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id, f.elements[1].id,
                                               f.elements[3].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());
    QCOMPARE(layouts.size(), 2);   // brand + params，无 Logo 占位
    QVERIFY(!layouts.contains(f.elements[1].id));
    const QRectF brandRect = layouts.value(f.elements[0].id).pxRect;
    const QRectF paramRect = layouts.value(f.elements[3].id).pxRect;
    // params 紧跟 brand（间距 ≈ rowH×0.2 = 10.8，允许度量误差）
    QVERIFY2(paramRect.left() - brandRect.right() < 15.0,
             "Logo 缺图后 params 应左移紧贴 brand，不留 2:1 占位空洞");
}

void TestLayout::rowOverflowShrinksFont() const
{
    // 超长镜头名：整行等比缩字（0.7~1.0×）且不越信息带右缘
    Fixture f = Fixture::make();
    f.fields.insert("lens",
                    QStringLiteral("OM Digital Solutions M.Zuiko ED 12-40mm F2.8 PRO")
                        .repeated(3));

    const LayoutRow row1 = f.makeRow(1, 0.4, { f.elements[4].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row1 }, f.plan,
                                               f.resolver(), f.logoResolver());
    const ElementLayout lensLayout = layouts.value(f.elements[4].id);
    QVERIFY2(lensLayout.fontScale < 1.0, "溢出行应缩字");
    QVERIFY2(lensLayout.fontScale >= 0.7 - 0.01, "缩字下限 0.7×");
    // 右缘约束（QRectF 语义 right = left+width）：band 右缘 = 25 + 950 = 975
    QVERIFY2(lensLayout.pxRect.right() <= 975.5,
             qPrintable(QStringLiteral("布局越右缘：right=%1").arg(lensLayout.pxRect.right())));
}

void TestLayout::customPositionOverridesRow() const
{
    Fixture f = Fixture::make();
    Element custom = f.elements[0];
    custom.id = QUuid::createUuid();
    custom.hasCustomPosition = true;
    custom.anchor = Anchor::BottomRight;
    custom.offset = QPointF(-0.3, -0.05);
    f.elements.append(custom);
    f.fields.insert(custom.fieldKey, "Canon");

    const LayoutRow row0 = f.makeRow(0, 0.6, { custom.id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());

    // 位置 = 锚点基准 + offset×画布（左上角语义），不参与行组
    const QRectF rect = layouts.value(custom.id).pxRect;
    const qreal expectLeft = 1000.0 + (-0.3) * 1000.0;    // 700
    const qreal expectTop = 840.0 + (-0.05) * 840.0;      // 798
    QCOMPARE(rect.left(), expectLeft);
    QCOMPARE(rect.top(), expectTop);
    QCOMPARE(layouts.size(), 1);
}

void TestLayout::singleLineModelBoost() const
{
    // 单行补偿（用户指令 2026-10-15）：镜头缺失时机型放大一号并跨带垂直
    // 居中。关键场景：行 2 右列日期仍在（行占位）——左块补偿按块级判定，
    // 不受行占位影响
    Fixture f = Fixture::make();
    f.fields.remove("lens");
    Element dateTime;
    dateTime.id = QUuid::createUuid();
    dateTime.kind = ElementKind::Text;
    dateTime.fieldKey = QStringLiteral("dateTime");
    f.elements.append(dateTime);
    f.fields.insert(QStringLiteral("dateTime"),
                    QStringLiteral("2013-11-25 13:46"));

    const LayoutRow row0 = f.makeRow(0, 0.5, { f.elements[0].id, f.elements[1].id,
                                               f.elements[2].id, f.elements[3].id });
    LayoutRow row1;
    row1.index = 1;
    row1.heightRatio = 0.5;
    row1.elementIds = { f.elements[4].id };   // lens（字段空 → 收紧剔除）
    row1.rightIds = { dateTime.id };          // 日期独行占位（行不剔除）

    const auto layouts = LayoutEngine::resolve(f.elements, { row0, row1 }, f.plan,
                                               f.resolver(), f.logoResolver());
    QCOMPARE(layouts.size(), 5);   // model/logo/divider/params + 日期

    // 放大系数进入布局结果（绘制字号 = 样式字号 × fontScale，度量同源）
    QCOMPARE(layouts.value(f.elements[0].id).fontScale, 1.15);
    // 左块跨带垂直居中（行 2 占位也不跟随行 1 对齐）
    const QRectF modelRect = layouts.value(f.elements[0].id).pxRect;
    const qreal bandTop = f.plan.photoRect.bottom() + 1.0;
    QVERIFY2(qAbs(modelRect.center().y()
                      - (bandTop + f.plan.exBandHeight / 2.0)) <= 1.0,
             "单行机型未跨信息带垂直居中");

    // 对照：镜头在场时不放大不跨带
    Fixture g = Fixture::make();
    const LayoutRow grow1 = f.makeRow(1, 0.5, { g.elements[4].id });
    const LayoutRow grow0 = f.makeRow(0, 0.5, { g.elements[0].id, g.elements[1].id,
                                                g.elements[2].id, g.elements[3].id });
    const auto layouts2 = LayoutEngine::resolve(g.elements, { grow0, grow1 },
                                                g.plan, g.resolver(),
                                                g.logoResolver());
    QCOMPARE(layouts2.value(g.elements[0].id).fontScale, 1.0);
}

void TestLayout::logoAspectRatio() const
{
    Fixture f = Fixture::make();
    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[1].id });   // 仅 Logo
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());

    // 高 = 0.5×54 = 27；2:1 图 → 宽 54
    const QRectF rect = layouts.value(f.elements[1].id).pxRect;
    QCOMPARE(rect.height(), 27.0);
    QCOMPARE(rect.width(), 54.0);
}

void TestLayout::dividerProportions() const
{
    Fixture f = Fixture::make();
    // 与 brand 同行（divider 依附行内元素，独行为空行被收紧剔除）
    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id, f.elements[2].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());

    // 宽 = 54×1.5% ≈ 0.81；高 = 54×60% = 32.4
    const QRectF rect = layouts.value(f.elements[2].id).pxRect;
    QCOMPARE(rect.width(), 54.0 * 0.015);
    QCOMPARE(rect.height(), 54.0 * 0.6);
}

void TestLayout::paintTextRendersPixels() const
{
    Fixture f = Fixture::make();
    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[0].id });
    const auto layouts = LayoutEngine::resolve(f.elements, { row0 }, f.plan,
                                               f.resolver(), f.logoResolver());

    const CanvasPlan plan = CanvasGeometry::plan(QLatin1String(kBottom), {1000, 750});
    QImage canvas = BackdropRenderer::backdrop(plan, QLatin1String(kBottom), QImage());
    QPainter painter(&canvas);
    ElementPainter::paintAll(&painter, f.elements, layouts, f.logoResolver(),
                             plan.canvasSize.width());
    painter.end();

    // 品牌文本区域应出现非背景色像素（黑色文字落在白底上）
    const QRectF r = layouts.value(f.elements[0].id).pxRect;
    bool found = false;
    for (int y = int(r.top()); y <= int(r.bottom()) && !found; ++y)
        for (int x = int(r.left()); x <= int(r.right()); ++x)
            if (canvas.pixel(x, y) != QColor(Qt::white).rgba()) {
                found = true;
                break;
            }
    QVERIFY(found);
}

QTEST_MAIN(TestLayout)
#include "test_layout.moc"
