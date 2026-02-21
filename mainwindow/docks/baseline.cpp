#include "mainwindow/docks/baseline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "mainwindow/docks/baselinelayout.h"

static QString coreDockAreaToString(Qt::DockWidgetArea area)
{
    switch (area) {
    case Qt::LeftDockWidgetArea: return QStringLiteral("left");
    case Qt::RightDockWidgetArea: return QStringLiteral("right");
    case Qt::TopDockWidgetArea: return QStringLiteral("top");
    case Qt::BottomDockWidgetArea: return QStringLiteral("bottom");
    default: break;
    }
    return QStringLiteral("none");
}

static QJsonObject baselineRectObject(const BaselineLayout::RectRule &rect)
{
    QJsonObject object;
    object.insert(QStringLiteral("x"), rect.x);
    object.insert(QStringLiteral("y"), rect.y);
    object.insert(QStringLiteral("width"), rect.width);
    object.insert(QStringLiteral("height"), rect.height);
    return object;
}

static QJsonObject baselineSizeObject(const BaselineLayout::SizeRule &size)
{
    QJsonObject object;
    object.insert(QStringLiteral("width"), size.width);
    object.insert(QStringLiteral("height"), size.height);
    return object;
}

static QJsonObject baselineSizingInfoObject(const BaselineLayout::DecodedLayoutNodeRule &node)
{
    QJsonObject object;
    object.insert(QStringLiteral("geometry"),
                  baselineRectObject({ node.x, node.y, node.width, node.height }));
    object.insert(QStringLiteral("minSize"),
                  baselineSizeObject({ node.minWidth, node.minHeight }));
    object.insert(QStringLiteral("maxSizeHint"),
                  baselineSizeObject({ node.maxWidth, node.maxHeight }));
    object.insert(QStringLiteral("percentageWithinParent"), node.percentageWithinParent);
    return object;
}

static QJsonObject baselineLayoutTreeNodeObject(int index)
{
    if (index < 0 || index >= static_cast<int>(BaselineLayout::kDecodedLayoutTree.size()))
        return QJsonObject();

    const BaselineLayout::DecodedLayoutNodeRule &node = BaselineLayout::kDecodedLayoutTree[index];
    QJsonObject object;
    object.insert(QStringLiteral("isContainer"), node.isContainer);
    object.insert(QStringLiteral("isVisible"), node.isVisible);
    object.insert(QStringLiteral("sizingInfo"), baselineSizingInfoObject(node));

    if (node.isContainer) {
        object.insert(QStringLiteral("orientation"), node.orientation);
        QJsonArray children;
        for (int i = 0; i < node.childCount && i < static_cast<int>(node.children.size()); ++i) {
            const int childIndex = node.children[i];
            if (childIndex >= 0)
                children.append(baselineLayoutTreeNodeObject(childIndex));
        }
        object.insert(QStringLiteral("children"), children);
    } else if (node.frameId && *node.frameId) {
        object.insert(QStringLiteral("guestId"), QString::fromLatin1(node.frameId));
    }

    return object;
}

static QJsonObject makeBaselineKddLayoutObject()
{
    QJsonObject layoutRoot;
    layoutRoot.insert(QStringLiteral("serializationVersion"), BaselineLayout::kSerializationVersion);

    QJsonArray screenInfo;
    for (const BaselineLayout::ScreenInfoRule &rule : BaselineLayout::kScreenInfoRules) {
        if (!rule.name)
            continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("index"), rule.index);
        entry.insert(QStringLiteral("name"), QString::fromLatin1(rule.name));
        entry.insert(QStringLiteral("devicePixelRatio"), rule.devicePixelRatio);
        entry.insert(QStringLiteral("geometry"), baselineRectObject(rule.geometry));
        screenInfo.append(entry);
    }
    layoutRoot.insert(QStringLiteral("screenInfo"), screenInfo);

    QJsonArray allDockWidgets;
    for (const BaselineLayout::AllDockWidgetRule &rule : BaselineLayout::kAllDockWidgetRules) {
        if (!rule.uniqueName)
            continue;
        QJsonObject dockWidget;
        dockWidget.insert(QStringLiteral("uniqueName"), QString::fromLatin1(rule.uniqueName));
        dockWidget.insert(QStringLiteral("lastCloseReason"), rule.lastCloseReason);

        QJsonObject lastPosition;
        lastPosition.insert(QStringLiteral("lastFloatingGeometry"),
                            baselineRectObject(rule.lastPosition.lastFloatingGeometry));
        lastPosition.insert(QStringLiteral("lastOverlayedGeometries"), QJsonArray());
        lastPosition.insert(QStringLiteral("tabIndex"), rule.lastPosition.tabIndex);
        lastPosition.insert(QStringLiteral("wasFloating"), rule.lastPosition.wasFloating);

        QJsonArray placeholders;
        if (rule.lastPosition.placeholderCount > 0) {
            QJsonObject placeholder;
            placeholder.insert(QStringLiteral("isFloatingWindow"), rule.lastPosition.placeholder.isFloatingWindow);
            placeholder.insert(QStringLiteral("itemIndex"), rule.lastPosition.placeholder.itemIndex);
            if (rule.lastPosition.placeholder.mainWindowUniqueName) {
                placeholder.insert(QStringLiteral("mainWindowUniqueName"),
                                   QString::fromLatin1(rule.lastPosition.placeholder.mainWindowUniqueName));
            }
            placeholders.append(placeholder);
        }
        lastPosition.insert(QStringLiteral("placeholders"), placeholders);
        dockWidget.insert(QStringLiteral("lastPosition"), lastPosition);
        allDockWidgets.append(dockWidget);
    }
    layoutRoot.insert(QStringLiteral("allDockWidgets"), allDockWidgets);

    QJsonArray closedDockWidgets;
    for (const char *name : BaselineLayout::kClosedDockWidgetNames) {
        if (name && *name)
            closedDockWidgets.append(QString::fromLatin1(name));
    }
    layoutRoot.insert(QStringLiteral("closedDockWidgets"), closedDockWidgets);

    layoutRoot.insert(QStringLiteral("floatingWindows"), QJsonArray());

    QJsonArray mainWindows;
    for (const BaselineLayout::MainWindowRule &mainWindowRule : BaselineLayout::kMainWindowRules) {
        if (!mainWindowRule.uniqueName)
            continue;
        QJsonObject mainWindow;
        mainWindow.insert(QStringLiteral("options"), mainWindowRule.options);
        mainWindow.insert(QStringLiteral("uniqueName"), QString::fromLatin1(mainWindowRule.uniqueName));
        mainWindow.insert(QStringLiteral("geometry"), baselineRectObject(mainWindowRule.geometry));
        mainWindow.insert(QStringLiteral("normalGeometry"), baselineRectObject(mainWindowRule.normalGeometry));
        mainWindow.insert(QStringLiteral("screenIndex"), mainWindowRule.screenIndex);
        mainWindow.insert(QStringLiteral("screenSize"), baselineSizeObject(mainWindowRule.screenSize));
        mainWindow.insert(QStringLiteral("isVisible"), mainWindowRule.isVisible);
        mainWindow.insert(QStringLiteral("affinities"), QJsonArray());
        mainWindow.insert(QStringLiteral("windowState"), mainWindowRule.windowState);

        QJsonObject multiSplitterLayout;
        multiSplitterLayout.insert(QStringLiteral("layout"),
                                   baselineLayoutTreeNodeObject(BaselineLayout::kDecodedLayoutRootNodeIndex));
        QJsonObject frames;
        for (const BaselineLayout::DecodedFrameRule &frame : BaselineLayout::kDecodedFrameRules) {
            if (!frame.frameId || !*frame.frameId || !frame.objectName || !*frame.objectName)
                continue;
            QJsonObject frameObject;
            frameObject.insert(QStringLiteral("id"), QString::fromLatin1(frame.frameId));
            frameObject.insert(QStringLiteral("isNull"), frame.isNull);
            frameObject.insert(QStringLiteral("objectName"), QString::fromLatin1(frame.objectName));
            frameObject.insert(QStringLiteral("geometry"),
                               baselineRectObject({ frame.x, frame.y, frame.width, frame.height }));
            frameObject.insert(QStringLiteral("options"), frame.options);
            frameObject.insert(QStringLiteral("currentTabIndex"), frame.currentTabIndex);
            if (frame.mainWindowUniqueName) {
                frameObject.insert(QStringLiteral("mainWindowUniqueName"),
                                   QString::fromLatin1(frame.mainWindowUniqueName));
            }

            QJsonArray dockWidgetsForFrame;
            for (int i = 0; i < frame.dockCount && i < static_cast<int>(frame.dockWidgets.size()); ++i) {
                const char *dockName = frame.dockWidgets[i];
                if (dockName && *dockName)
                    dockWidgetsForFrame.append(QString::fromLatin1(dockName));
            }
            frameObject.insert(QStringLiteral("dockWidgets"), dockWidgetsForFrame);
            frames.insert(QString::fromLatin1(frame.frameId), frameObject);
        }
        multiSplitterLayout.insert(QStringLiteral("frames"), frames);
        mainWindow.insert(QStringLiteral("multiSplitterLayout"), multiSplitterLayout);
        mainWindows.append(mainWindow);
    }
    layoutRoot.insert(QStringLiteral("mainWindows"), mainWindows);
    return layoutRoot;
}

QByteArray makeBaselineKddLayoutBytes()
{
    return QJsonDocument(makeBaselineKddLayoutObject()).toJson(QJsonDocument::Compact);
}

QJsonObject makeBaselineDebugDockStateObject()
{
    QJsonArray docks;

    for (const BaselineLayout::DebugDockStateRule &rule : BaselineLayout::kDebugDockStateRules) {
        if (!rule.dockId)
            continue;

        QJsonObject customState;
        const auto addInt = [&customState](const char *key, int value) {
            if (value != BaselineLayout::kUnsetInt)
                customState.insert(QString::fromLatin1(key), value);
        };
        const auto addBoolFlag = [&customState](const char *key, int value) {
            if (value == 0 || value == 1)
                customState.insert(QString::fromLatin1(key), value == 1);
        };
        const auto addString = [&customState](const char *key, const char *value) {
            if (value)
                customState.insert(QString::fromLatin1(key), QString::fromLatin1(value));
        };

        addString("baseAddr", rule.baseAddr);
        addString("searchText", rule.searchText);
        addInt("displayFormat", rule.displayFormat);
        addInt("modeIndex", rule.modeIndex);
        addInt("searchType", rule.searchType);
        addInt("selectedOffset", rule.selectedOffset);
        addBoolFlag("showAscii", rule.showAscii);
        addString("filterText", rule.filterText);
        addInt("fontSize", rule.fontSize);
        if (rule.includeEmptyCommandHistory)
            customState.insert(QStringLiteral("commandHistory"), QJsonArray());
        addInt("maxBlockCount", rule.maxBlockCount);
        addBoolFlag("autoRefresh", rule.autoRefresh);
        addInt("bpp", rule.bpp);
        addInt("imageHeight", rule.imageHeight);
        addInt("imageWidth", rule.imageWidth);
        addInt("zoom", rule.zoom);
        addInt("refreshIndex", rule.refreshIndex);

        QJsonObject item;
        item.insert(QStringLiteral("dockId"), QString::fromLatin1(rule.dockId));
        item.insert(QStringLiteral("customState"), customState);
        docks.append(item);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(BaselineLayout::kDebugDockStateSchema));
    root.insert(QStringLiteral("docks"), docks);
    return root;
}

QJsonObject makeBaselineCoreDockConnectionsObject()
{
    QJsonArray pairs;
    for (const BaselineLayout::CoreDockConnectionRule &rule : BaselineLayout::kCoreDockConnectionRules) {
        if (!rule.a || !rule.b)
            continue;
        QJsonObject pair;
        pair.insert(QStringLiteral("a"), QString::fromLatin1(rule.a));
        pair.insert(QStringLiteral("b"), QString::fromLatin1(rule.b));
        pair.insert(QStringLiteral("area"), coreDockAreaToString(rule.area));
        pairs.append(pair);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(BaselineLayout::kCoreDockConnectionsSchema));
    root.insert(QStringLiteral("pairs"), pairs);
    return root;
}
