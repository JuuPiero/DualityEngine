// UIDocument (markup parser, stylesheet parser/cascade, Instantiate pipeline) regression
// coverage.
#include "TestFramework.h"

#include <filesystem>
#include <fstream>

#include "DualityEngine/Renderer/UIRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/UI/UIDocument.h"
#include "DualityEngine/UI/UIMarkupParser.h"
#include "DualityEngine/UI/UIStylesheetParser.h"

using namespace Duality;

namespace {
    std::string TempUiDocPath() {
        return (std::filesystem::temp_directory_path() / "duality_engine_test_ui.uidoc").string();
    }
    void WriteFile(const std::string& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
    }
}

TEST_CASE("UIMarkupParser builds a tree with correct tags/ids/classes/attributes") {
    UIMarkupParseResult result = UIMarkupParser::Parse(
        "<ui>"
        "  <Panel id=\"root\" class=\"hud\" x=\"5\" y=\"10\">"
        "    <Button id=\"playBtn\" class=\"primary big\"/>"
        "  </Panel>"
        "</ui>");

    CHECK(result.Success);
    CHECK(result.Root.Children.size() == 1);
    const UIElementNode& panel = result.Root.Children[0];
    CHECK_SOFT(panel.Tag == "Panel", "root child tag parsed correctly");
    CHECK_SOFT(panel.Id == "root", "id attribute extracted, not left in Attributes");
    CHECK_SOFT(panel.Classes.size() == 1 && panel.Classes[0] == "hud", "single class parsed");
    CHECK_SOFT(panel.Attributes.at("x") == "5", "plain attribute preserved");
    CHECK_SOFT(panel.Attributes.count("id") == 0, "id is not ALSO left in Attributes");
    CHECK(panel.Children.size() == 1);
    CHECK_SOFT(panel.Children[0].Id == "playBtn", "nested element parsed");
    CHECK_SOFT(panel.Children[0].Classes.size() == 2, "multiple space-separated classes parsed");
}

TEST_CASE("UIMarkupParser pulls <style> content out of the tree into InlineStylesheetSources") {
    UIMarkupParseResult result = UIMarkupParser::Parse(
        "<ui>"
        "  <style>.btn { color: #ff0000; }</style>"
        "  <Button class=\"btn\"/>"
        "</ui>");

    CHECK(result.Success);
    CHECK(result.InlineStylesheetSources.size() == 1);
    CHECK_SOFT(result.InlineStylesheetSources[0].find("#ff0000") != std::string::npos, "raw CSS text captured verbatim");
    CHECK_SOFT(result.Root.Children.size() == 1, "the <style> element itself does not appear in the tree");
    CHECK_SOFT(result.Root.Children[0].Tag == "Button", "the real content is still there");
}

TEST_CASE("UIMarkupParser reports a parse error on mismatched closing tags") {
    UIMarkupParseResult result = UIMarkupParser::Parse("<ui><Panel></Button></ui>");
    CHECK_SOFT(!result.Success, "mismatched tags should fail, not silently produce a wrong tree");
    CHECK_SOFT(!result.Error.empty(), "failure includes a reason");
}

TEST_CASE("UIStylesheetParser parses element/class/id selectors with correct specificity") {
    UIStylesheet sheet;
    UIStylesheetParser::ParseInto("Panel { color: #111111; } .primary { color: #222222; } #playBtn { color: #333333; }", sheet);

    CHECK(sheet.size() == 3);
    CHECK_SOFT(sheet[0].Selector.SelectorKind == UIStyleSelector::Kind::Element, "element selector kind");
    CHECK_SOFT(sheet[0].Selector.Specificity() == 1, "element specificity");
    CHECK_SOFT(sheet[1].Selector.SelectorKind == UIStyleSelector::Kind::Class, "class selector kind");
    CHECK_SOFT(sheet[1].Selector.Specificity() == 10, "class specificity");
    CHECK_SOFT(sheet[2].Selector.SelectorKind == UIStyleSelector::Kind::Id, "id selector kind");
    CHECK_SOFT(sheet[2].Selector.Specificity() == 100, "id specificity, highest");
    CHECK_SOFT(sheet[2].Declarations.at("color") == "#333333", "declaration value parsed");
}

TEST_CASE("UIDocument Instantiate creates entities with the right components from tags") {
    std::string path = TempUiDocPath();
    WriteFile(path,
        "<ui>"
        "  <Panel id=\"root\" screen=\"Bottom\" anchor=\"top-left\" x=\"0\" y=\"0\" width=\"320\" height=\"240\" color=\"#204060\">"
        "    <Button id=\"playBtn\" anchor=\"bottom-right\" x=\"10\" y=\"10\" width=\"80\" height=\"32\" normal-color=\"#5aa9ff\"/>"
        "    <Text id=\"label\" text=\"Score: 0\"/>"
        "  </Panel>"
        "</ui>");

    UIDocument doc = UIDocument::Load(path);
    CHECK(doc.IsLoaded());

    Scene scene;
    Entity docRoot = doc.Instantiate(scene, Screen::Top); // Panel's own screen="Bottom" should override this default
    CHECK(docRoot);

    Entity panel = docRoot.GetComponent<HierarchyComponent>().Children.empty() ? Entity{} : docRoot.GetComponent<HierarchyComponent>().Children[0];
    CHECK(panel);
    CHECK_SOFT(panel.HasComponent<UIRectComponent>() && panel.HasComponent<UIImageComponent>(), "Panel gets UIRect+UIImage");
    CHECK_SOFT(!panel.HasComponent<UIButtonComponent>(), "Panel does NOT get UIButton");
    auto& panelRect = panel.GetComponent<UIRectComponent>();
    CHECK_SOFT(panelRect.Screen == Screen::Bottom, "explicit screen=\"Bottom\" attribute overrides Instantiate's default screen");
    glm::vec2 panelResolvedTopLeft, panelResolvedSize;
    ResolveUIRect(scene, panel, panelResolvedTopLeft, panelResolvedSize);
    CHECK_SOFT(panelResolvedSize.x == 320.0f && panelResolvedSize.y == 240.0f, "width/height attributes applied");
    CHECK_SOFT(panel.GetComponent<UIImageComponent>().Color.b > 0.2f, "color attribute parsed (hex #204060 has a real blue component)");

    CHECK(panel.GetComponent<HierarchyComponent>().Children.size() == 2);
    Entity button = panel.GetComponent<HierarchyComponent>().Children[0];
    CHECK_SOFT(button.HasComponent<UIButtonComponent>(), "Button gets UIButtonComponent");
    CHECK_SOFT(button.GetComponent<UIButtonComponent>().NormalColor.b > 0.9f, "normal-color attribute applied (hex #5aa9ff has a strong blue component)");
    CHECK_SOFT(button.GetComponent<UIRectComponent>().Screen == Screen::Bottom, "child with no explicit screen inherits Instantiate's screen, same as its parent");

    Entity text = panel.GetComponent<HierarchyComponent>().Children[1];
    CHECK_SOFT(text.HasComponent<UITextComponent>() && !text.HasComponent<UIImageComponent>(), "Text gets UITextComponent only, no UIImage");
    CHECK_SOFT(text.GetComponent<UITextComponent>().Text == "Score: 0", "text attribute applied");

    std::filesystem::remove(path);
}

TEST_CASE("UIDocument CSS cascade: class beats element, inline style beats everything") {
    std::string path = TempUiDocPath();
    WriteFile(path,
        "<ui>"
        "  <style>"
        "    Panel { width: 50; }"
        "    .wide { width: 200; }"
        "  </style>"
        "  <Panel class=\"wide\" style=\"width: 999;\"/>"
        "  <Panel class=\"wide\"/>"
        "  <Panel/>"
        "</ui>");

    UIDocument doc = UIDocument::Load(path);
    CHECK(doc.IsLoaded());
    Scene scene;
    Entity docRoot = doc.Instantiate(scene, Screen::Top);
    auto& children = docRoot.GetComponent<HierarchyComponent>().Children;
    CHECK(children.size() == 3);
    glm::vec2 childTopLeft, childSize;
    ResolveUIRect(scene, children[0], childTopLeft, childSize);
    CHECK_SOFT(childSize.x == 999.0f, "inline style wins over both class and element rules");
    ResolveUIRect(scene, children[1], childTopLeft, childSize);
    CHECK_SOFT(childSize.x == 200.0f, "class selector (specificity 10) beats element selector (specificity 1)");
    ResolveUIRect(scene, children[2], childTopLeft, childSize);
    CHECK_SOFT(childSize.x == 50.0f, "plain element rule still applies with no class/inline override");

    std::filesystem::remove(path);
}

TEST_CASE("UIDocument nesting: a child's rect resolves relative to its parent's rect, not the raw screen") {
    std::string path = TempUiDocPath();
    WriteFile(path,
        "<ui>"
        "  <Panel anchor=\"top-left\" x=\"50\" y=\"20\" width=\"200\" height=\"100\">"
        "    <Button anchor=\"top-left\" x=\"5\" y=\"5\" width=\"40\" height=\"20\"/>"
        "  </Panel>"
        "</ui>");

    UIDocument doc = UIDocument::Load(path);
    Scene scene;
    Entity docRoot = doc.Instantiate(scene, Screen::Top);
    Entity panel = docRoot.GetComponent<HierarchyComponent>().Children[0];
    Entity button = panel.GetComponent<HierarchyComponent>().Children[0];

    glm::vec2 panelTopLeft, panelSize, buttonTopLeft, buttonSize;
    ResolveUIRect(scene, panel, panelTopLeft, panelSize);
    ResolveUIRect(scene, button, buttonTopLeft, buttonSize);

    CHECK_SOFT(panelTopLeft.x == 50.0f && panelTopLeft.y == 20.0f, "root-level panel resolves against the screen as before");
    CHECK_SOFT(buttonTopLeft.x == 55.0f && buttonTopLeft.y == 25.0f, "nested button's (5,5) offset is relative to the PARENT's top-left (50,20), i.e. (55,25) in screen space, not (5,5)");

    std::filesystem::remove(path);
}

TEST_CASE("UIDocument fails gracefully for a nonexistent file") {
    UIDocument doc = UIDocument::Load("this_uidoc_does_not_exist.uidoc");
    CHECK_SOFT(!doc.IsLoaded(), "missing file should not crash, just report unloaded");
    Scene scene;
    Entity result = doc.Instantiate(scene, Screen::Top);
    CHECK_SOFT(!result, "Instantiate on a failed load is a no-op returning an empty Entity");
}
