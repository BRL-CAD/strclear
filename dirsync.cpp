/*                     D I R S Y N C . C P P
 * BRL-CAD
 *
 * Published in 2025 by the United States Government.
 * This work is in the public domain.
 *
 */
/** @file dirsync.cpp
 *
 * This utility is intended to do the work of keeping a bext
 * install directory and the copy of those files in the
 * BRL-CAD build directory up to date.
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
#include <regex>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <system_error>
#include <optional>
#include <string>

#include "cxxopts.hpp"

namespace fs = std::filesystem;

// --- Struct to hold program options ---
struct DirSyncOptions {
    bool verbose_initial = false;
    std::optional<std::string> listfile_out;
    std::vector<std::regex> excl;
};

// --- Utility functions ---
bool
is_excluded(const fs::path& rel, const std::vector<std::regex>& excl) {
    return std::any_of(
        excl.begin(), excl.end(),
        [&](const std::regex& rx) {
            return std::regex_match(rel.string(), rx);
        }
    );
}

void
copy_perms(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::permissions(dst, fs::status(src).permissions(), ec);
}

void
copy_mtime(const fs::path& dst, const fs::path& src) {
    std::error_code ec;
    auto t = fs::last_write_time(src, ec);
    if (!ec)
	fs::last_write_time(dst, t, ec);
}

void file_copy(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

// --- Collect all relative paths (including symlinks) ---
void
gather_paths(const fs::path& root, std::set<fs::path>& out, const std::vector<std::regex>& excl)
{
    for (auto& e : fs::directory_iterator(root)) {
	auto rel = e.path().lexically_relative(root);
	if (!is_excluded(rel, excl))
	    out.insert(rel);

	if (fs::is_directory(e.path()) && !fs::is_symlink(e.path())) {
	    for (auto& se : fs::recursive_directory_iterator(e.path())) {
		auto srel = se.path().lexically_relative(root);
		if (!is_excluded(srel, excl))
		    out.insert(srel);
	    }
	}
    }
}

// --- Sync using mtime+size only ---
// Verbose applies to initial copy (add actions) if target dir is empty or does not exist
// If listfile_out is set, write (canonic(target_dir) / rel_path) for all added and changed
void
sync_dirs(const fs::path& src, const fs::path& dst, const DirSyncOptions& options)
{
    std::set<fs::path> srcs, dsts;
    std::vector<fs::path> listfile_paths; // Collect added & changed paths for optional write

    gather_paths(src, srcs, options.excl);

    bool dst_exists = fs::exists(dst);
    if (dst_exists)
	gather_paths(dst, dsts, options.excl);

    bool initial_copy = !dst_exists || dsts.empty();

    std::set<fs::path> add, rm, mod;
    for (const auto& p : srcs)
	if (dsts.count(p) == 0) add.insert(p);
    for (const auto& p : dsts)
	if (srcs.count(p) == 0) rm.insert(p);

    for (const auto& p : srcs) {
	if (dsts.count(p)) {
	    auto sp = src / p, dp = dst / p;
	    if (fs::is_regular_file(sp) && fs::is_regular_file(dp)) {
		std::error_code ec1, ec2, ec3, ec4;
		auto st = fs::last_write_time(sp, ec1);
		auto dt = fs::last_write_time(dp, ec2);
		auto ss = fs::file_size(sp, ec3);
		auto ds = fs::file_size(dp, ec4);
		if (ec1 || ec2 || ec3 || ec4 || st != dt || ss != ds)
		    mod.insert(p);
	    }
	    else if (fs::is_symlink(sp) && fs::is_symlink(dp)) {
		std::error_code ec1, ec2;
		auto stgt = fs::read_symlink(sp, ec1);
		auto dtgt = fs::read_symlink(dp, ec2);
		if (ec1 || ec2 || stgt != dtgt)
		    mod.insert(p);
	    }
	    else if ((fs::is_regular_file(sp) != fs::is_regular_file(dp)) ||
		     (fs::is_directory(sp) != fs::is_directory(dp)) ||
		     (fs::is_symlink(sp) != fs::is_symlink(dp)))
		mod.insert(p);
	}
    }

    // Get canonical target root for output
    fs::path canonical_dst;
    std::error_code canon_ec;
    canonical_dst = fs::weakly_canonical(dst, canon_ec);
    if (canon_ec) canonical_dst = fs::absolute(dst);

    for (const auto& p : rm) {
	auto dp = dst / p;
	std::error_code ec;
	fs::remove_all(dp, ec);
	std::cout << "[rm] " << dp << "\n";
    }
    for (const auto& p : add) {
	auto sp = src / p, dp = dst / p;
	std::error_code ec;
	if (fs::is_directory(sp) && !fs::is_symlink(sp)) {
	    fs::create_directories(dp, ec);
	    copy_perms(sp, dp);
	    if (!initial_copy || options.verbose_initial)
	        std::cout << "[add] dir " << dp << "\n";
	    if (options.listfile_out) listfile_paths.push_back(canonical_dst / p);
	} else if (fs::is_symlink(sp)) {
	    auto tgt = fs::read_symlink(sp, ec);
	    if (ec) { std::cerr << "Warn: " << ec.message() << "\n"; continue; }
	    fs::remove(dp, ec);
	    fs::create_symlink(tgt, dp, ec);
	    if (!initial_copy || options.verbose_initial)
	        std::cout << "[add] link " << dp << " -> " << tgt << "\n";
	    if (options.listfile_out) listfile_paths.push_back(canonical_dst / p);
	} else if (fs::is_regular_file(sp)) {
	    fs::create_directories(dp.parent_path(), ec);
	    file_copy(sp, dp);
	    copy_perms(sp, dp);
	    copy_mtime(dp, sp);
	    if (!initial_copy || options.verbose_initial)
	        std::cout << "[add] file " << dp << "\n";
	    if (options.listfile_out) listfile_paths.push_back(canonical_dst / p);
	}
    }
    for (const auto& p : mod) {
	auto sp = src / p, dp = dst / p;
	std::error_code ec;
	if (fs::is_regular_file(sp)) {
	    file_copy(sp, dp);
	    copy_perms(sp, dp);
	    copy_mtime(dp, sp);
	    std::cout << "[chg] file " << dp << "\n";
	    if (options.listfile_out) listfile_paths.push_back(canonical_dst / p);
	}
	else if (fs::is_symlink(sp)) {
	    auto tgt = fs::read_symlink(sp, ec);
	    fs::remove(dp, ec);
	    fs::create_symlink(tgt, dp, ec);
	    std::cout << "[chg] link " << dp << " -> " << tgt << "\n";
	    if (options.listfile_out) listfile_paths.push_back(canonical_dst / p);
	}
	else if (fs::is_directory(sp) && !fs::is_symlink(sp)) {
	    copy_perms(sp, dp);
	    std::cout << "[chg] dir " << dp << "\n";
	    if (options.listfile_out) listfile_paths.push_back(canonical_dst / p);
	}
    }

    // Write listfile if requested
    if (options.listfile_out) {
        std::ofstream lf(*options.listfile_out);
        if (!lf) {
            std::cerr << "Error: couldn't open list file: " << *options.listfile_out << "\n";
        } else {
            for (const auto& path : listfile_paths)
                lf << path.string() << "\n";
        }
    }
}

// --- Fix absolute symlinks ---
void
fix_symlinks(const fs::path& dst_root, const fs::path& src_root)
{
    auto canonical_src = fs::weakly_canonical(src_root);
    auto canonical_dst = fs::weakly_canonical(dst_root);

    for (auto& entry : fs::recursive_directory_iterator(dst_root)) {
        if (!fs::is_symlink(entry.path()))
            continue;

        std::error_code ec;
        fs::path link_target = fs::read_symlink(entry.path(), ec);
        if (ec) continue;

        if (link_target.is_absolute()) {
            // Is this link targeting a file inside the source tree?
            auto link_target_canon = fs::weakly_canonical(link_target, ec);
            if (ec) continue;

            // Check if link_target_canon begins with canonical_src
            auto mismatch = std::mismatch(
                canonical_src.begin(), canonical_src.end(),
                link_target_canon.begin()
            );

            if (mismatch.first == canonical_src.end()) {
                // Get the path inside the source tree
                fs::path inside_src_rel;
                for (; mismatch.second != link_target_canon.end(); ++mismatch.second)
                    inside_src_rel /= *mismatch.second;

                // Compute the equivalent target in the dst tree
                fs::path dst_target = canonical_dst / inside_src_rel;

                // Compute the relative path from symlink's parent to new target
                fs::path rel = dst_target.lexically_relative(entry.path().parent_path());

                // Replace symlink
                fs::remove(entry.path(), ec);
                fs::create_symlink(rel, entry.path(), ec);
                std::cout << "[fixlink] " << entry.path() << " -> " << rel << "\n";
            }
        }
    }
}

int
main(int argc, char** argv)
{
    cxxopts::Options opts("dirsync", "Directory sync utility for BRL-CAD build trees");

    opts.add_options()
      ("v,verbose", "Enable verbose logging on initial copy", cxxopts::value<bool>()->default_value("false"))
      ("l,listfile", "Output list of added and changed paths to file", cxxopts::value<std::string>())
      ("x,exclude", "Regex for exclude pattern (repeatable)", cxxopts::value<std::vector<std::string>>())
      ("src", "Source directory", cxxopts::value<std::string>())
      ("dst", "Target directory", cxxopts::value<std::string>())
      ("h,help", "Print help");

    opts.parse_positional({"src", "dst"});
    opts.positional_help("<src> <dst>");
    opts.show_positional_help();

    auto result = opts.parse(argc, argv);

    if (result.count("help") || !result.count("src") || !result.count("dst")) {
        std::cerr << opts.help() << std::endl;
        return 1;
    }

    DirSyncOptions options;
    options.verbose_initial = result["verbose"].as<bool>();
    if (result.count("listfile")) {
        options.listfile_out = result["listfile"].as<std::string>();
    }
    if (result.count("exclude")) {
        for (const auto& ex : result["exclude"].as<std::vector<std::string>>()) {
            options.excl.emplace_back(ex);
        }
    } else {
        // Default: exclude hidden files
        options.excl.emplace_back("^\\..*");
    }

    fs::path src = result["src"].as<std::string>();
    fs::path dst = result["dst"].as<std::string>();

    std::cout << "Sync: " << src << " -> " << dst << "\n";
    sync_dirs(src, dst, options);

    // Optionally fix relative symlinks (todo - make this an actual option, but default to on...)
    fix_symlinks(dst,src);

    std::cout << "Done.\n";
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
