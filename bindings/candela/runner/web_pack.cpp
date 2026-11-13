#include "web_pack.hpp"

#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace drift::web {

namespace {

struct Entry {
    std::string path;   // in the pack: relative to the game folder, '/' separated
    fs::path source;
};

struct Project {
    std::string entry = "main.cdl";
    std::string assets = "assets";
    std::string name;
};

bool readFile(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool writeFile(const fs::path& path, const std::string& data) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    o.write(data.data(), std::streamsize(data.size()));
    return bool(o);
}

bool hidden(const fs::path& name) { return name.string().rfind('.', 0) == 0; }

// Build output folders: build and build-<anything>.
bool buildOutput(const fs::path& name) {
    std::string n = name.string();
    return n == "build" || n.rfind("build-", 0) == 0;
}

bool readProject(const fs::path& file, Project& p, std::string& error) {
    if (!fs::is_regular_file(file)) return true;
    try {
        toml::table t = toml::parse_file(file.string());
        if (auto v = t["project"]["entry"].value<std::string>()) p.entry = *v;
        if (auto v = t["project"]["assets"].value<std::string>()) p.assets = *v;
        if (auto v = t["project"]["name"].value<std::string>()) p.name = *v;
    } catch (const toml::parse_error& e) {
        error = file.string() + ": " + std::string(e.description());
        return false;
    }
    return true;
}

std::string escapeHtml(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

void putU32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out += char((v >> (8 * i)) & 0xff);
}

bool sameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

} // namespace

bool buildWebPage(const std::string& folderArg, const std::string& outArg, const std::string& runtimeArg,
                  std::string& error, std::string& summary) {
    std::error_code ec;
    fs::path folder = fs::absolute(folderArg, ec);
    if (!fs::is_directory(folder)) {
        error = folderArg + ": not a folder";
        return false;
    }
    fs::path out = fs::absolute(outArg, ec);
    fs::path runtime = fs::absolute(runtimeArg, ec);
    for (const char* name : {"drift.html", "drift.js", "drift.wasm"}) {
        if (!fs::is_regular_file(runtime / name)) {
            error = "no web runtime at " + runtime.string() + " (it needs drift.html, drift.js and drift.wasm)";
            return false;
        }
    }

    Project project;
    if (!readProject(folder / "drift.toml", project, error)) return false;
    fs::path assets = fs::path(project.assets).lexically_normal();
    if (!assets.has_filename()) assets = assets.parent_path();   // "assets/"

    std::vector<Entry> files;
    if (fs::is_regular_file(folder / "drift.toml")) files.push_back({"drift.toml", folder / "drift.toml"});

    // Every script, at its path in the folder. Hidden folders, build output,
    // the page being written and the asset folder (packed whole below) are
    // skipped.
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::follow_directory_symlink, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path& p = it->path();
        fs::path rel = p.lexically_relative(folder);
        if (it->is_directory()) {
            if (hidden(p.filename()) || buildOutput(p.filename()) || rel == assets || sameFolder(p, out)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (it->is_regular_file() && p.extension() == ".cdl" && !hidden(p.filename())) {
            files.push_back({rel.generic_string(), p});
        }
    }
    if (ec) {
        error = folder.string() + ": " + ec.message();
        return false;
    }
    if (std::none_of(files.begin(), files.end(), [&](const Entry& e) {
            return e.path == fs::path(project.entry).lexically_normal().generic_string();
        })) {
        error = (folder / project.entry).string() + ": no such script; drift.toml [project] entry names the game's first script";
        return false;
    }

    // The asset folder, everything in it but hidden files.
    size_t assetCount = 0;
    if (fs::is_directory(folder / assets)) {
        for (auto it = fs::recursive_directory_iterator(folder / assets, fs::directory_options::follow_directory_symlink, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const fs::path& p = it->path();
            if (hidden(p.filename())) {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file()) {
                files.push_back({p.lexically_relative(folder).generic_string(), p});
                ++assetCount;
            }
        }
        if (ec) {
            error = (folder / assets).string() + ": " + ec.message();
            return false;
        }
    }
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });

    // The pack: header, index, then every file's bytes.
    nlohmann::json index = nlohmann::json::array();
    std::string blob;
    for (const Entry& f : files) {
        std::string data;
        if (!readFile(f.source, data)) {
            error = f.source.string() + ": cannot read";
            return false;
        }
        index.push_back({{"path", f.path}, {"offset", blob.size()}, {"size", data.size()}});
        blob += data;
    }
    std::string indexText = nlohmann::json{{"files", index}}.dump();
    std::string pack = "DRIFTPAK";
    putU32(pack, 1);
    putU32(pack, uint32_t(indexText.size()));
    pack += indexText;
    pack += blob;

    fs::create_directories(out, ec);
    if (!fs::is_directory(out)) {
        error = out.string() + ": cannot make the folder";
        return false;
    }
    if (!writeFile(out / "game.pack", pack)) {
        error = (out / "game.pack").string() + ": cannot write";
        return false;
    }
    for (const char* name : {"drift.js", "drift.wasm"}) {
        fs::copy_file(runtime / name, out / name, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = (out / name).string() + ": " + ec.message();
            return false;
        }
    }
    std::string page;
    if (!readFile(runtime / "drift.html", page)) {
        error = (runtime / "drift.html").string() + ": cannot read";
        return false;
    }
    const std::string title = "<title>Drift</title>";
    if (!project.name.empty()) {
        size_t at = page.find(title);
        if (at != std::string::npos) page.replace(at, title.size(), "<title>" + escapeHtml(project.name) + "</title>");
    }
    if (!writeFile(out / "index.html", page)) {
        error = (out / "index.html").string() + ": cannot write";
        return false;
    }

    summary = out.string() + ": index.html, drift.js, drift.wasm and game.pack (" +
              std::to_string(files.size() - assetCount) + " scripts and settings, " +
              std::to_string(assetCount) + " assets)";
    return true;
}

} // namespace drift::web
