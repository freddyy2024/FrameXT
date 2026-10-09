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
        // band = (0, 751, 1000, 90)；两行 0.6/0.4 → row0: y=751 h=54, row1: y=805 h=36

        Element brand;
        brand.id = QUuid::createUuid();
        brand.kind = ElementKind::Text;
        brand.fieldKey = QStringLiteral("make");
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

        f.fields.insert("make", "Canon");
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
    void customPositionOverridesRow() const;
    void logoAspectRatio() const;
    void dividerProportions() const;
    void paintTextRendersPixels() const;
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

    // 第一行元素垂直居中于 row0（y=751, h=54）→ 中心 y ≈ 778（字体度量取整允许 ±1）
    const QRectF brandRect = layouts.value(f.elements[0].id).pxRect;
    QVERIFY2(qAbs(brandRect.center().y() - 778.0) <= 1.0, "brand 垂直居中偏差过大");
    // 第二行（lens）垂直居中于 row1（y=805, h=36）→ 中心 ≈ 823（允许 ±1）
    const QRectF lensRect = layouts.value(f.elements[4].id).pxRect;
    QVERIFY2(qAbs(lensRect.center().y() - 823.0) <= 1.0, "lens 垂直居中偏差过大");
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

    // 收紧后只排 brand 与 params；brand 仍从行左端开始，params 紧随其后
    QCOMPARE(layouts.size(), 2);
    const QRectF brandRect = layouts.value(f.elements[0].id).pxRect;
    const QRectF paramRect = layouts.value(f.elements[3].id).pxRect;
    QCOMPARE(brandRect.left(), 0.0);
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
    const LayoutRow row0 = f.makeRow(0, 0.6, { f.elements[2].id });   // 仅分隔线
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
