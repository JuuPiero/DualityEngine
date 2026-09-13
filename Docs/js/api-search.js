// Client-side search for the generated public-header index. It intentionally
// uses no network requests so Docs works when opened directly from disk.
(function () {
    "use strict";

    var headers = Array.isArray(window.DualityApiIndex) ? window.DualityApiIndex : [];
    var query = document.getElementById("api-query");
    var filters = document.getElementById("api-module-filters");
    var results = document.getElementById("api-results");
    var count = document.getElementById("api-result-count");
    var typesOnly = document.getElementById("api-types-only");
    var functionsOnly = document.getElementById("api-functions-only");
    var showAll = document.getElementById("api-show-all");
    if (!query || !filters || !results || !count) return;

    var selectedModule = "All";
    var limit = 18;
    var modules = ["All"];
    headers.forEach(function (header) {
        if (modules.indexOf(header.module) === -1) modules.push(header.module);
    });
    modules.sort(function (a, b) { return a === "All" ? -1 : b === "All" ? 1 : a.localeCompare(b); });

    function make(tag, className, text) {
        var element = document.createElement(tag);
        if (className) element.className = className;
        if (text !== undefined) element.textContent = text;
        return element;
    }

    function renderFilters() {
        filters.textContent = "";
        modules.forEach(function (module) {
            var button = make("button", "api-filter" + (module === selectedModule ? " active" : ""), module);
            button.type = "button";
            button.addEventListener("click", function () {
                selectedModule = module;
                limit = 18;
                renderFilters();
                renderResults();
            });
            filters.appendChild(button);
        });
    }

    function matches(header, term) {
        if (selectedModule !== "All" && header.module !== selectedModule) return false;
        var symbols = header.symbols || [];
        if (typesOnly.checked && !symbols.some(function (symbol) { return symbol.kind === "type"; })) return false;
        if (functionsOnly.checked && !symbols.some(function (symbol) { return symbol.kind === "function"; })) return false;
        if (!term) return true;
        var searchable = [header.path, header.module, header.name, header.source]
            .concat(symbols.map(function (symbol) { return symbol.name; }))
            .join("\n").toLocaleLowerCase();
        return searchable.indexOf(term) !== -1;
    }

    function appendBadges(parent, symbols) {
        var visible = symbols.slice(0, 16);
        visible.forEach(function (symbol) {
            parent.appendChild(make("span", "api-symbol " + symbol.kind, symbol.name));
        });
        if (symbols.length > visible.length) {
            parent.appendChild(make("span", "api-symbol more", "+" + (symbols.length - visible.length)));
        }
    }

    function renderResults() {
        var term = query.value.trim().toLocaleLowerCase();
        var matched = headers.filter(function (header) { return matches(header, term); });
        var rendered = matched.slice(0, limit);
        results.textContent = "";
        count.textContent = matched.length + " header phù hợp · " + headers.length + " public header đã được index";
        showAll.hidden = matched.length <= limit;

        if (!matched.length) {
            results.appendChild(make("p", "dim", "Không có API phù hợp. Thử tên type, hàm hoặc một phần declaration."));
            return;
        }

        rendered.forEach(function (header) {
            var card = make("article", "api-file");
            var heading = make("div", "api-file-head");
            var title = make("h2", "api-file-title", header.name);
            title.style.borderTop = "none";
            title.style.margin = "0";
            heading.appendChild(title);
            heading.appendChild(make("span", "badge badge-static", header.module));
            card.appendChild(heading);
            card.appendChild(make("div", "api-file-path mono", header.path));

            var symbolList = make("div", "api-symbols");
            appendBadges(symbolList, header.symbols || []);
            card.appendChild(symbolList);

            var details = make("details", "api-source");
            details.appendChild(make("summary", "", "Mở public header đầy đủ"));
            details.appendChild(make("pre", "", header.source));
            card.appendChild(details);
            results.appendChild(card);
        });

        if (matched.length > rendered.length) {
            var more = make("button", "api-load-more", "Hiện thêm " + Math.min(18, matched.length - rendered.length) + " header");
            more.type = "button";
            more.addEventListener("click", function () { limit += 18; renderResults(); });
            results.appendChild(more);
        }
    }

    [query, typesOnly, functionsOnly].forEach(function (control) {
        control.addEventListener("input", function () { limit = 18; renderResults(); });
        control.addEventListener("change", function () { limit = 18; renderResults(); });
    });
    showAll.addEventListener("click", function () { limit = headers.length; renderResults(); });

    renderFilters();
    renderResults();
})();
