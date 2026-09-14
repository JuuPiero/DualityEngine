// Shared sidebar navigation. Pages work from file:// as well as a static host.
(function () {
    "use strict";
    var isRoot = !/\/pages\//.test(location.pathname);
    var pages = isRoot ? "pages/" : "";
    var root = isRoot ? "" : "../";
    var navigation = [
        { group: "Bắt đầu", items: [
            { id: "home", label: "Tổng quan", href: root + "index.html", icon: "Home" },
            { id: "getting-started", label: "Cài đặt & Build", href: pages + "getting-started.html", icon: "Build" },
            { id: "first-game", label: "Game đầu tiên", href: pages + "first-game-tutorial.html", icon: "Play" }
        ]},
        { group: "Engine", items: [
            { id: "architecture", label: "ECS & Scene", href: pages + "architecture.html", icon: "ECS" },
            { id: "api-stack", label: "API Stack", href: pages + "api-stack.html", icon: "Stack" },
            { id: "api-reference", label: "API Reference", href: pages + "api-reference.html", icon: "API" }
        ]},
        { group: "Hệ thống", items: [
            { id: "scripting-api", label: "Scripting", href: pages + "scripting-api.html", icon: "Code" },
            { id: "serialization-references", label: "Serialization & References", href: pages + "serialization-references.html", icon: "Ref" },
            { id: "physics", label: "Physics 2D / 3D", href: pages + "physics.html", icon: "Physics" },
            { id: "rendering", label: "Rendering & UI", href: pages + "rendering.html", icon: "Render" },
            { id: "assets", label: "Assets & Packages", href: pages + "assets.html", icon: "Assets" },
            { id: "components", label: "Components", href: pages + "components.html", icon: "Components" }
        ]},
        { group: "Editor & Deploy", items: [
            { id: "editor", label: "Editor", href: pages + "editor.html", icon: "Editor" },
            { id: "build-deploy", label: "Build & Deploy", href: pages + "build-deploy.html", icon: "3DS" }
        ]},
        { group: "Thiết kế tương lai", items: [
            { id: "multiplayer-design", label: "Multiplayer & 3DS Network", href: pages + "multiplayer-design.html", icon: "Network" }
        ]}
    ];
    var current = document.body.getAttribute("data-page") || "";
    var html = '<div class="sidebar-brand"><img class="logo" alt="DualityEngine" src="' + root + 'images/logo.png"><div class="name">DualityEngine<small>Documentation</small></div></div>';
    html += '<a class="docs-search-link" href="' + pages + 'api-reference.html">Tìm API, class, hàm</a>';
    navigation.forEach(function (section) {
        html += '<div class="nav-group"><div class="nav-group-title">' + section.group + '</div>';
        section.items.forEach(function (item) {
            html += '<a class="nav-link' + (item.id === current ? ' active' : '') + '" href="' + item.href + '"><span class="nav-icon">' + item.icon + '</span>' + item.label + '</a>';
        });
        html += '</div>';
    });
    var container = document.getElementById("sidebar-root");
    if (container) container.innerHTML = html;
})();
