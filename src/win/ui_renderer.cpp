// Written against the C++/WinRT projection of Windows App SDK 1.8.
// Ampify.ui's tree on screen: each render reconciles the new tree with the last one and patches the elements in place.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer_detail.hpp"

namespace ampify::win {

XamlRenderer::XamlRenderer(std::string elementId, UiEventSink sink)
    : m_elementId(std::move(elementId)), m_sink(std::move(sink)), m_updating(std::make_shared<int>(0)) {}

XamlRenderer::~XamlRenderer() = default;

xaml::FrameworkElement XamlRenderer::Root() const {
    return m_root ? m_root->outer : nullptr;
}

xaml::FrameworkElement XamlRenderer::Render(RenderNode tree, const StyleProbe& probe, uirender::PatchStats& stats,
                                            bool& rootReplaced) {
    auto next = std::make_unique<RenderNode>(std::move(tree));
    rootReplaced = false;
    UpdateGuard guard(*m_updating);

    bool rebuild = !m_root || !m_root->outer || (m_tree && uirender::LayoutOf(*next) != m_root->layout);
    Patch patch = uirender::Reconcile(rebuild ? nullptr : m_tree.get(), *next);
    stats = uirender::CountPatch(patch);
    if (rebuild || patch.op == PatchOp::Create || patch.op == PatchOp::Replace) {
        m_root = std::make_unique<Mounted>(Build(*next, probe));
        rootReplaced = true;
    } else {
        Apply(*m_root, patch, probe);
    }
    if (m_root->outer) Place(m_root->outer, *next, TypePlacement(*next));
    m_tree = std::move(next);
    return Root();
}

// Sets the cells of a Grid layout (Auto, or Star for a growing child), and the cell and the place of each child. The
// parent decides the defaults: the second child of a pair on the right, the children of a row centred vertically,
// the children of a layer stretched over it.
void XamlRenderer::PlaceChildren(Mounted& mounted, const RenderNode& node) {
    auto grid = mounted.host ? mounted.host.try_as<controls::Grid>() : nullptr;
    bool horizontal = node.type == "row";
    if (mounted.layout == Layout::Columns) SetGridCells(mounted, node);
    if (grid && mounted.layout == Layout::Grid) {
        std::vector<bool> cells;
        for (const RenderNode& child : node.children) cells.push_back(child.Bool("grow", false));
        if (cells != mounted.cells) {
            grid.ColumnDefinitions().Clear();
            grid.RowDefinitions().Clear();
            for (bool grows : cells) {
                xaml::GridLength length{1.0, grows ? xaml::GridUnitType::Star : xaml::GridUnitType::Auto};
                if (horizontal) {
                    controls::ColumnDefinition column;
                    column.Width(length);
                    grid.ColumnDefinitions().Append(column);
                } else {
                    controls::RowDefinition row;
                    row.Height(length);
                    grid.RowDefinitions().Append(row);
                }
            }
            mounted.cells = std::move(cells);
        }
    }
    std::size_t count = std::min(mounted.children.size(), node.children.size());
    for (std::size_t index = 0; index < count; index++) {
        xaml::FrameworkElement element = mounted.children[index].outer;
        if (!element) continue;
        const RenderNode& child = node.children[index];
        Placement placement = TypePlacement(child);
        int cell = static_cast<int>(index);
        if (mounted.layout == Layout::Pair || (mounted.layout == Layout::Grid && horizontal)) {
            if (controls::Grid::GetColumn(element) != cell) controls::Grid::SetColumn(element, cell);
            placement.vertical = xaml::VerticalAlignment::Center;
            if (mounted.layout == Layout::Pair && index == 1) placement.horizontal = xaml::HorizontalAlignment::Right;
            // A growing child takes the space left, a button included.
            if (child.Bool("grow", false)) placement.horizontal = xaml::HorizontalAlignment::Stretch;
        } else if (mounted.layout == Layout::Grid) {
            if (controls::Grid::GetRow(element) != cell) controls::Grid::SetRow(element, cell);
        } else if (mounted.layout == Layout::Layer) {
            placement = Placement{};
        } else if (mounted.layout == Layout::Columns) {
            uirender::GridCell at = uirender::GridCellOf(index, mounted.columns);
            if (controls::Grid::GetRow(element) != at.row) controls::Grid::SetRow(element, at.row);
            if (controls::Grid::GetColumn(element) != at.column) controls::Grid::SetColumn(element, at.column);
            placement.horizontal = xaml::HorizontalAlignment::Stretch;  // a cell takes the width of its column
        }
        Place(element, child, placement);
    }
}

// The cells of a grid: `columns` equal Star columns and one Auto row per line of children, with the gap between them.
void XamlRenderer::SetGridCells(Mounted& mounted, const RenderNode& node) {
    auto grid = mounted.host ? mounted.host.try_as<controls::Grid>() : nullptr;
    if (!grid) return;
    int columns = uirender::GridColumns(node);
    std::size_t lines = (node.children.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns);
    int rows = static_cast<int>(lines);
    double gap = node.Number("gap", 8);
    if (grid.ColumnSpacing() != gap) grid.ColumnSpacing(gap);
    if (grid.RowSpacing() != gap) grid.RowSpacing(gap);
    if (columns == mounted.columns && rows == mounted.rows) return;
    grid.ColumnDefinitions().Clear();
    grid.RowDefinitions().Clear();
    for (int i = 0; i < columns; i++) {
        controls::ColumnDefinition column;
        column.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        grid.ColumnDefinitions().Append(column);
    }
    for (int i = 0; i < rows; i++) {
        controls::RowDefinition row;
        row.Height(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        grid.RowDefinitions().Append(row);
    }
    mounted.columns = columns;
    mounted.rows = rows;
}

// background, padding, cornerRadius and the size of stack, row, layer and card. A card falls back to its own look.
void XamlRenderer::SetBox(Mounted& mounted, const RenderNode& node) {
    xaml::FrameworkElement element = mounted.outer;
    std::string background = node.Text("background");
    if (background != mounted.background) {
        media::Brush brush = background.empty() ? mounted.baseBackground : MakeBackground(background);
        SetPanelBackground(element, brush ? brush : mounted.baseBackground);
        mounted.background = background;
    }
    xaml::Thickness padding = mounted.basePadding;
    if (node.Has("padding")) {
        auto found = node.attributes.find("padding");
        if (std::optional<uirender::Edges> edges = uirender::ParsePadding(found->second)) {
            padding = xaml::Thickness{edges->left, edges->top, edges->right, edges->bottom};
        }
    }
    SetPanelPadding(element, padding);
    xaml::CornerRadius corner = mounted.baseCorner;
    if (node.Has("cornerRadius")) {
        double radius = node.Number("cornerRadius", 0);
        corner = xaml::CornerRadius{radius, radius, radius, radius};
    }
    corner = {std::min(corner.TopLeft, m_cornerLimit), std::min(corner.TopRight, m_cornerLimit),
              std::min(corner.BottomRight, m_cornerLimit), std::min(corner.BottomLeft, m_cornerLimit)};
    SetPanelCorner(element, corner);
    double width = node.Number("width", kAuto);
    double height = node.Number("height", kAuto);
    double minWidth = node.Number("minWidth", 0);
    double maxWidth = node.Number("maxWidth", kInfinity);
    if (!SameLength(element.Width(), width)) element.Width(width);
    if (!SameLength(element.Height(), height)) element.Height(height);
    if (element.MinWidth() != minWidth) element.MinWidth(minWidth);
    if (element.MaxWidth() != maxWidth) element.MaxWidth(maxWidth);
}

void XamlRenderer::Apply(Mounted& mounted, const Patch& patch, const StyleProbe& probe) {
    const RenderNode& node = *patch.node;
    if (patch.op == PatchOp::Update) SetAttributes(mounted, node, probe, false);

    if (node.type == "dropdown" || node.type == "menu") {  // their entries are not children: refilled on a change
        bool entriesChanged = patch.reordered;
        for (const Patch& child : patch.children) entriesChanged = entriesChanged || child.op != PatchOp::Keep;
        // A menu's flyout is rebuilt only for its entries: a redraw would close it while the user has it open.
        if (entriesChanged || (patch.op == PatchOp::Update && node.type == "dropdown")) {
            if (node.type == "menu") FillMenu(mounted, node);
            else FillOptions(mounted, node);
        }
        return;
    }
    if (!mounted.host) return;

    bool rewrite = patch.reordered;
    std::vector<Mounted> next;
    next.reserve(patch.children.size());
    for (const Patch& child : patch.children) {
        bool reuse = child.from >= 0 && child.op != PatchOp::Create && child.op != PatchOp::Replace &&
                     child.from < static_cast<int>(mounted.children.size());
        if (reuse) {
            Mounted& old = mounted.children[child.from];
            // A container whose layout changes (a row of 2 children that gets a third, a child that starts to grow)
            // changes its native panel: rebuilt.
            bool layoutChanged = uirender::LayoutOf(*child.node) != old.layout;
            if (old.outer && !layoutChanged) {
                Apply(old, child, probe);
                next.push_back(std::move(old));
                continue;
            }
        }
        next.push_back(Build(*child.node, probe));
        rewrite = true;
    }
    mounted.children = std::move(next);
    if (rewrite) Rewrite(mounted);
    PlaceChildren(mounted, node);  // a child's align, valign or grow may change without any move
}

void XamlRenderer::Rewrite(Mounted& mounted) {
    controls::Panel host = mounted.host;
    if (!host) return;
    auto children = host.Children();
    children.Clear();
    for (const Mounted& child : mounted.children) {
        if (xaml::FrameworkElement element = child.outer) children.Append(element);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
