// Shared sidebar navigation for every Docs page. Each page includes:
//   <body data-page="page-id">
//   <div id="sidebar-root"></div>   (as the first child inside .shell)
//   <script src=".../js/nav.js"></script>   (placed right before </body>, so #sidebar-root
//                                             already exists in the DOM by the time this runs)
(function () {
    var isRoot = !/\/pages\//.test(location.pathname);
    var base = isRoot ? "pages/" : "";
    var rootBase = isRoot ? "" : "../";

    var NAV = [
        { group: "Bắt đầu", items: [
            { id: "home", label: "Tổng quan", href: rootBase + "index.html", icon: "🏠" },
            { id: "getting-started", label: "Cài đặt & Build", href: base + "getting-started.html", icon: "⚙️" },
            { id: "first-game", label: "Làm game đầu tiên", href: base + "first-game-tutorial.html", icon: "🎮" }
        ]},
        { group: "Kiến trúc", items: [
            { id: "architecture", label: "ECS & Scene", href: base + "architecture.html", icon: "🧩" }
        ]},
        { group: "API Reference", items: [
            { id: "scripting-api", label: "Scripting (Behaviour)", href: base + "scripting-api.html", icon: "📜" },
            { id: "physics", label: "Physics 2D/3D", href: base + "physics.html", icon: "🪐" },
            { id: "rendering", label: "Rendering & UI", href: base + "rendering.html", icon: "🖼️" },
            { id: "assets", label: "Asset Pipeline", href: base + "assets.html", icon: "📦" },
            { id: "components", label: "Components Reference", href: base + "components.html", icon: "🧱" }
        ]},
        { group: "Editor & Deploy", items: [
            { id: "editor", label: "Editor Panels", href: base + "editor.html", icon: "🛠️" },
            { id: "build-deploy", label: "Build & Deploy", href: base + "build-deploy.html", icon: "🚀" }
        ]}
    ];

    var currentPage = document.body.getAttribute("data-page") || "";

    function render() {
        var html = '<div class="sidebar-brand">' +
            '<img class="logo" src="' + rootBase + 'images/logo.png" />' +
            '<div class="name">DualityEngine<small>Docs</small></div>' +
            '</div>';
        for (var g = 0; g < NAV.length; g++) {
            var group = NAV[g];
            html += '<div class="nav-group"><div class="nav-group-title">' + group.group + '</div>';
            for (var i = 0; i < group.items.length; i++) {
                var item = group.items[i];
                var active = item.id === currentPage ? " active" : "";
                html += '<a class="nav-link' + active + '" href="' + item.href + '">' +
                    '<span>' + item.icon + '</span>' + item.label + '</a>';
            }
            html += '</div>';
        }
        return html;
    }

    var root = document.getElementById("sidebar-root");
    if (root) root.innerHTML = render();
})();
