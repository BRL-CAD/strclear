/*                  S T R C L E A R . C P P
 * BRL-CAD
 *
 * Copyright (c) 2018-2023 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/** @file strclear.cpp
 *
 * This tool has two primary jobs:
 *
 * Given a binary file and a string, replace any instances of the string in the
 * binary with null chars (or a different character specified by an option.)
 *
 * Given a text file, a target string and an optional replacement string,
 * replace all instances of the target string with the replacement string (or
 * remove the target string if there is no replacement string - it is
 * "replaced" with the empty string.)
 *
 * The --classify mode emits JSON Lines records for build systems that need to
 * batch text/binary detection.
 */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "cxxopts.hpp"

struct process_opts {
    bool binary_only = false;
    bool binary_test_mode = false;
    bool clear_mode = false;
    bool classify_mode = false;
    bool force_binary = false;
    bool force_text = false;
    bool path_mode = false;
    bool text_only = false;
    bool verbose = false;
    char clear_char = '\0';
    std::vector<std::string> tgt_strs;
    std::string replace_str;
};

static std::string
json_escape(const std::string &input)
{
    std::ostringstream out;
    for (unsigned char c : input) {
	switch (c) {
	    case '"':
		out << "\\\"";
		break;
	    case '\\':
		out << "\\\\";
		break;
	    case '\b':
		out << "\\b";
		break;
	    case '\f':
		out << "\\f";
		break;
	    case '\n':
		out << "\\n";
		break;
	    case '\r':
		out << "\\r";
		break;
	    case '\t':
		out << "\\t";
		break;
	    default:
		if (c < 0x20) {
		    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c << std::dec;
		} else {
		    out << c;
		}
	}
    }
    return out.str();
}

template <typename T>
class WorkQueue {
    std::queue<T> q;
    std::mutex m;
    std::condition_variable cv;
    bool closed = false;

public:
    bool push(T v) {
	std::lock_guard<std::mutex> lg(m);
	if (closed) return false;
	q.push(std::move(v));
	cv.notify_one();
	return true;
    }

    bool pop(T &out) {
	std::unique_lock<std::mutex> lk(m);
	cv.wait(lk, [&]{ return closed || !q.empty(); });
	if (q.empty()) return false;
	out = std::move(q.front());
	q.pop();
	return true;
    }

    void close() {
	std::lock_guard<std::mutex> lg(m);
	closed = true;
	cv.notify_all();
    }
};

inline std::vector<std::string>
expand_path_forms(const std::string &input) {
    namespace fs = std::filesystem;
    std::vector<std::string> forms;
    if (!input.length())
	return forms;

    auto add_form = [&](const fs::path &p) {
	std::string s = p.string();
	if (!s.empty())
	    forms.push_back(s);
    };

    auto add_partial_canonical = [&](const fs::path &p) {
	std::error_code ec;
	fs::path probe = fs::absolute(p, ec);
	if (ec)
	    probe = p;

	fs::path suffix;
	while (!probe.empty() && !fs::exists(probe, ec)) {
	    fs::path parent = probe.parent_path();
	    if (parent == probe)
		break;
	    if (suffix.empty())
		suffix = probe.filename();
	    else
		suffix = probe.filename() / suffix;
	    probe = parent;
	    ec.clear();
	}

	if (probe.empty() || !fs::exists(probe, ec))
	    return;

	ec.clear();
	fs::path canon = fs::canonical(probe, ec);
	if (ec || canon.empty())
	    return;

	if (!suffix.empty())
	    canon /= suffix;
	add_form(canon);
    };

    try {
	fs::path p(input);

	// Always include the original spelling, plus cheap normalized forms.
	add_form(p);
	std::error_code ec;
	fs::path abs = fs::absolute(p, ec);
	if (!ec)
	    add_form(abs);
	add_form(p.lexically_normal());
	if (!ec)
	    add_form(abs.lexically_normal());

	ec.clear();
	if (fs::exists(p, ec)) {
	    fs::path canon = fs::canonical(p, ec);
	    if (!ec)
		add_form(canon);
	}

	add_partial_canonical(p);
    } catch (...) {
	// Ignore errors (broken symlink, permission denied, etc).
    }

    // Match longer paths first so shorter path spellings do not partially
    // replace longer ones.
    std::sort(forms.begin(), forms.end(),
	        [](const std::string &a, const std::string &b) {
		   if (a.size() != b.size())
		      return a.size() > b.size(); // longer first
		   return a > b; // lexicographical descending
	        }
	     );
    forms.erase(std::unique(forms.begin(), forms.end()), forms.end());

    return forms;
}

inline std::vector<std::string>
expand_target_strings(const std::vector<std::string> &targets, bool path_mode)
{
    std::vector<std::string> expanded;
    if (!path_mode) {
	expanded = targets;
    } else {
	for (const auto &target : targets) {
	    std::vector<std::string> forms = expand_path_forms(target);
	    expanded.insert(expanded.end(), forms.begin(), forms.end());
	}
    }

    std::sort(expanded.begin(), expanded.end(),
	      [](const std::string &a, const std::string &b) {
		  if (a.size() != b.size())
		      return a.size() > b.size();
		  return a > b;
	      });
    expanded.erase(std::unique(expanded.begin(), expanded.end()), expanded.end());
    return expanded;
}

int
process_binary(const std::string &fname, process_opts &p)
{
    // Keep track of how many times we change the file - that's our return code
    int change_cnt = 0;

    // Read binary contents
    std::ifstream input_fs;
    input_fs.open(fname, std::ios::binary);
    if (!input_fs.is_open()) {
	std::cerr << "Unable to open file " << fname << "\n";
	return change_cnt;
    }
    std::vector<char> bin_contents(std::istreambuf_iterator<char>(input_fs), {});
    input_fs.close();

    // Process all target strings
    for (const auto &tgt : p.tgt_strs) {
	std::vector<char> search_chars(tgt.begin(), tgt.end());
	std::vector<char> null_chars(search_chars.size(), p.clear_char);

	// Find instances of target string in binary, and replace any we find
	auto position = bin_contents.begin();
	while ((position = std::search(position, bin_contents.end(), search_chars.begin(), search_chars.end())) != bin_contents.end()) {
	    std::copy(null_chars.begin(), null_chars.end(), position);
	    position += search_chars.size();
	    // For clear ops we count by decrementing so we know what to print
	    // - a file is only cleared or replaced, not both, and a binary
	    // file can only be cleared.
	    change_cnt--;
	}
    }
    if (change_cnt == 0)
	return 0;

    // If we changed the contents, write them back out
    std::ofstream output_fs;
    output_fs.open(fname, std::ios::binary);
    if (!output_fs.is_open()) {
	std::cerr << "Unable to write updated file contents for " << fname << "\n";
	return change_cnt;
    }

    std::copy(bin_contents.begin(), bin_contents.end(), std::ostreambuf_iterator<char>(output_fs));
    output_fs.close();

    return change_cnt;
}

int
process_text(const std::string &fname, process_opts &p)
{
    // Keep track of how many times we change the file - that's our return code
    int change_cnt = 0;

    // Read text contents
    std::ifstream input_fs(fname);
    if (!input_fs.is_open()) {
	std::cerr << "Unable to open file " << fname << "\n";
	return change_cnt;
    }
    std::stringstream fbuffer;
    fbuffer << input_fs.rdbuf();
    std::string nfile_contents = fbuffer.str();
    input_fs.close();
    if (!nfile_contents.length())
	return change_cnt;

    // For replace ops we count by incrementing and clear opts we count by
    // decrementing, so we know what to print in the tally.  A file is only
    // cleared or replaced, not both, and which it is depends on the
    // replacement string.
    int rincr = (p.replace_str.length()) ? 1 : -1;

    // Use index and std::string::find for O(N) replacement
    for (const auto &tgt : p.tgt_strs) {
	// If we're replacing a string with itself, that's a no-op
	if (tgt == p.replace_str)
	    continue;
	size_t pos = 0;
	while ((pos = nfile_contents.find(tgt, pos)) != std::string::npos) {
	    nfile_contents.replace(pos, tgt.size(), p.replace_str);
	    pos += p.replace_str.size();
	    change_cnt += rincr;
	}
    }
    if (change_cnt == 0)
	return 0;

    // If we changed the contents, write them back out
    std::ofstream output_fs;
    output_fs.open(fname, std::ios::trunc);
    if (!output_fs.is_open()) {
	std::cerr << "Unable to write updated file contents for " << fname << "\n";
	exit(-1);
    }
    output_fs << nfile_contents;
    output_fs.close();
    return change_cnt;
}

bool
is_binary(std::ifstream &file, size_t max_check = 4096, double nontext_threshold = 0.1)
{
    size_t n_read = 0;
    size_t n_nontext = 0;
    char c;
    while (n_read < max_check && file.get(c)) {
	n_read++;
	// Null byte: almost always binary
	if (c == '\0')
	    return true;
	// Accept printable ASCII, CR, LF, TAB and FF.
	if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t' || c == '\f')
	    continue;
	// Treat common UTF-8 lead bytes as text unless other bytes dominate.
	if ((unsigned char)c >= 0xC2 && (unsigned char)c <= 0xF4)
	    continue;
	n_nontext++;
    }

    if (n_read == 0)
	return false; // empty file: treat as text

    return (double)n_nontext / n_read > nontext_threshold;
}

void
process_files(std::map<std::string, std::atomic<int>> &op_tally, std::set<std::string> &files, process_opts &p)
{
    if (files.empty() || !p.tgt_strs.size())
	return;

    unsigned int num_threads = (unsigned int)(0.5 * (double)std::thread::hardware_concurrency());
    if (num_threads == 0)
	num_threads = 4;
    num_threads = std::min(num_threads, (unsigned int)files.size());

    // Pre-populate op_tally entries so worker threads only update atomics.
    for (const auto &fname : files) {
	(void)op_tally[fname];           // default-construct atomic<int> (0)
	op_tally[fname].store(0, std::memory_order_relaxed);
    }

    WorkQueue<std::string> wq;
    for (const auto &fname : files) {
	bool ok = wq.push(fname);
	(void)ok; // with our usage we haven't called close yet; ok should be true
    }
    wq.close();

    auto worker = [&]() {
	std::string fname;
	while (wq.pop(fname)) {
	    std::ifstream check_fs(fname, std::ios::binary);
	    if (!check_fs.is_open()) {
		std::cerr << "Error:  unable to open " << fname << "\n";
		op_tally[fname].store(0, std::memory_order_relaxed);
		continue;
	    }
	    bool binary_mode = (p.force_binary) ? true : is_binary(check_fs);
	    check_fs.close();

	    int result = 0;
	    if (binary_mode && p.force_text) {
		std::cerr << "Error: string replacement indicated, but file is binary: " << fname << "\n";
		op_tally[fname].store(0, std::memory_order_relaxed);
		continue;
	    }
	    if (binary_mode && !p.text_only)
		result = process_binary(fname, p);
	    if (!binary_mode && !p.binary_only)
		result = process_text(fname, p);
	    op_tally[fname].store(result, std::memory_order_relaxed);
	}
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (unsigned int i = 0; i < num_threads; ++i)
	threads.emplace_back(worker);

    for (auto &t : threads)
	t.join();
}

int
main(int argc, const char *argv[])
{
    process_opts p;
    bool legacy_binary_mode = false;
    std::string file_list;

    cxxopts::Options options(argv[0],
	    "Clear or replace strings in files.\n"
	    "\n"
	    "strclear -B <filename>\n"
	    "strclear --classify [--files <filelist> | <filename> ...]\n"
	    "strclear <filename> <target_str> [replacement_str]\n"
	    "strclear -f <filelist> <target_str> [replacement_str]\n"
	    "\n"
	    "-B returns 0 for binary input and 1 for text input.\n"
	    "--classify emits one JSON record per input path.\n"
	    );

    std::vector<std::string> nonopts;

    try
    {
	options
	    .set_width(70)
	    .add_options()
	    ("B,is_binary",   "Test one file: return 0 for binary input and 1 for text input.", cxxopts::value<bool>(p.binary_test_mode))
	    ("t,text",        "Compatibility alias: force text replacement mode.", cxxopts::value<bool>(p.force_text))
	    ("text-only",     "Skip inputs that are binary files.", cxxopts::value<bool>(p.text_only))
	    ("b,binary",      "Compatibility alias: force binary clear mode.", cxxopts::value<bool>(legacy_binary_mode))
	    ("binary-only",   "Skip inputs that are text files.", cxxopts::value<bool>(p.binary_only))
	    ("c,clear",       "Compatibility option for binary clear mode.", cxxopts::value<bool>(p.clear_mode))
	    ("classify",      "Classify files as TEXT or BINARY, one JSON record per path.", cxxopts::value<bool>(p.classify_mode))
	    ("r,replace",     "Compatibility option for text replacement mode.", cxxopts::value<bool>())
	    ("f,files",       "Provide a list of files to process.", cxxopts::value<std::string>(file_list))
	    ("clear-char",    "Specify a character to use when clearing strings in files", cxxopts::value<char>(p.clear_char))
	    ("clear_char",    "Specify a character to use when clearing strings in files", cxxopts::value<char>(p.clear_char))
	    ("p,paths",       "Expand target strings that are file paths into recognized forms (original, absolute, canonical, normalized).", cxxopts::value<bool>(p.path_mode))
	    ("v,verbose",     "Verbose reporting during processing", cxxopts::value<bool>(p.verbose))
	    ("h,help",        "Print help")
	    ;
	auto result = options.parse(argc, argv);

	// Do we want help?
	if (result.count("help")) {
	    std::cout << options.help({""}) << std::endl;
	    return 0;
	}

	nonopts = result.unmatched();
	if (legacy_binary_mode)
	    p.force_binary = true;
	if (p.force_binary)
	    p.binary_only = false;
	if (result.count("replace")) {
	    if (p.clear_mode) {
		std::cerr << "Error: need to specify either clear or replace mode, not both.\n";
		return -1;
	    }
	    p.force_text = true;
	}
    }
    catch (const cxxopts::exceptions::exception& e)
    {
	std::cerr << "error parsing options: " << e.what() << std::endl;
	return -1;
    }

    /////////////////////////////////////////
    // Do some option checking and validation
    /////////////////////////////////////////

    if (p.classify_mode && p.binary_test_mode) {
	std::cerr << "Error:  specify either -B or --classify, not both.\n";
	return -1;
    }
    if (p.classify_mode && (legacy_binary_mode || p.clear_mode || p.force_text || p.text_only || p.binary_only || p.path_mode)) {
	std::cerr << "Error:  --classify cannot be combined with rewrite mode options.\n";
	return -1;
    }

    // binary_only ∩ text_only == NULL set
    if ((p.binary_only || p.force_binary) && (p.text_only || p.force_text)) {
	std::cerr << "Error:  can specify binary-only or text-only, not both.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }

    // If we're testing whether a file is binary, we only take one argument
    if (p.binary_test_mode && nonopts.size() != 1) {
	std::cerr << "Error:  -B accepts exactly one file path as input.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if (p.binary_test_mode) {
	std::ifstream check_fs(nonopts[0], std::ios::binary);
	if (!check_fs.is_open()) {
	    std::cerr << "Unable to open file " << nonopts[0] << "\n";
	    return -1;
	}
	bool binary_mode = is_binary(check_fs);
	check_fs.close();
	return binary_mode ? 0 : 1;
    }

    if (p.classify_mode) {
	std::vector<std::string> files;
	if (file_list.length()) {
	    if (nonopts.size()) {
		std::cerr << "Error:  specify either --files or file paths for --classify, not both.\n";
		return -1;
	    }
	    std::ifstream instream(file_list);
	    if (!instream.is_open()) {
		std::cerr << "Error: Could not open " << file_list << "\n";
		return -1;
	    }
	    std::string line;
	    while (std::getline(instream, line)) {
		if (line.length())
		    files.push_back(line);
	    }
	    instream.close();
	} else {
	    for (const auto &fname : nonopts)
		files.push_back(fname);
	}
	if (files.empty()) {
	    std::cerr << "Error:  --classify needs at least one file path.\n";
	    return -1;
	}

	// Classification is I/O bound (each file reads at most a few KB), so
	// for large file lists - which is exactly when build systems call
	// --classify in batch - the sequential cost of opening thousands of
	// files dominates.  Fan the work out across threads the same way
	// process_files() does.  Results are stored by index so the emitted
	// JSON records stay in input order regardless of completion order.
	std::vector<int> results(files.size(), 0); // 1 = binary, 0 = text, -1 = error
	std::atomic<size_t> next_idx(0);
	unsigned int num_threads = (unsigned int)(0.5 * (double)std::thread::hardware_concurrency());
	if (num_threads == 0)
	    num_threads = 4;
	num_threads = std::min(num_threads, (unsigned int)files.size());

	auto classify_worker = [&]() {
	    size_t i;
	    while ((i = next_idx.fetch_add(1, std::memory_order_relaxed)) < files.size()) {
		std::ifstream check_fs(files[i], std::ios::binary);
		if (!check_fs.is_open()) {
		    std::cerr << "Unable to open file " << files[i] << "\n";
		    results[i] = -1;
		    continue;
		}
		results[i] = is_binary(check_fs) ? 1 : 0;
		check_fs.close();
	    }
	};

	std::vector<std::thread> classify_threads;
	classify_threads.reserve(num_threads);
	for (unsigned int t = 0; t < num_threads; ++t)
	    classify_threads.emplace_back(classify_worker);
	for (auto &t : classify_threads)
	    t.join();

	int ret = 0;
	for (size_t i = 0; i < files.size(); i++) {
	    if (results[i] < 0) {
		ret = -1;
		continue;
	    }
	    std::cout << "{\"type\":\"" << (results[i] == 1 ? "BINARY" : "TEXT") << "\",\"path\":\"" << json_escape(files[i]) << "\"}\n";
	}
	return ret;
    }

    // Everything else needs at least a filename or file list and a target string
    if ((file_list.length() && !p.binary_only && !p.force_binary) && (nonopts.size() != 1 && nonopts.size() != 2)) {
	std::cerr << "Error:  when using a file list we need a target string and (optionally) a replacement string.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((file_list.length() && (p.binary_only || p.force_binary)) && nonopts.empty()) {
	std::cerr << "Error:  binary file-list processing needs at least one target string.\n";
	return -1;
    }
    if ((!file_list.length() && !p.binary_only && !p.force_binary) && (nonopts.size() != 2 && nonopts.size() != 3)) {
	std::cerr << "Error:  we need a file, a target string and (optionally) a replacement string.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((!file_list.length() && p.binary_only) && (nonopts.size() != 2)) {
	std::cerr << "Error:  when in binary-only mode we only accept a filename, a target string and (optionally) a --clear-char character.\n";
	return -1;
    }
    if ((!file_list.length() && p.force_binary) && (nonopts.size() < 2)) {
	std::cerr << "Error:  when in binary mode we need a filename and one or more target strings.\n";
	return -1;
    }
    if (p.force_text && !file_list.length() && nonopts.size() != 3) {
	std::cerr << "Error:  replacing string in text file - need file, target string and replacement string as arguments.\n";
	return -1;
    }
    if (p.force_text && file_list.length() && nonopts.size() != 2) {
	std::cerr << "Error:  replacing strings in a text file list needs target string and replacement string arguments.\n";
	return -1;
    }

    std::set<std::string> files;
    std::string target_str;
    if (!file_list.length()) {
	files.insert(nonopts[0]);
	target_str = nonopts[1];
	if (p.force_binary) {
	    for (size_t i = 1; i < nonopts.size(); i++)
		p.tgt_strs.push_back(nonopts[i]);
	} else {
	    p.replace_str = (nonopts.size() > 2) ? std::string(nonopts[2]) : std::string("");
	}
    } else {
	std::ifstream instream(file_list);
	if (!instream.is_open()) {
	    std::cerr << "Error: Could not open " << file_list << "\n";
	    return -1;
	}
	std::string line;
	while (std::getline(instream, line))
	    files.insert(line);
	instream.close();

	target_str = nonopts[0];
	if (p.force_binary || p.binary_only) {
	    for (size_t i = 0; i < nonopts.size(); i++)
		p.tgt_strs.push_back(nonopts[i]);
	} else if (!p.binary_only) {
	    p.replace_str = (nonopts.size() > 1) ? std::string(nonopts[1]) : std::string("");
	}
    }

    if (!p.force_binary && !target_str.length()) {
	std::cerr << "Error: empty target string supplied\n";
	return -1;
    }

    if (!p.force_binary && !(file_list.length() && p.binary_only)) {
	p.tgt_strs.clear();
	p.tgt_strs.push_back(target_str);
    }
    p.tgt_strs = expand_target_strings(p.tgt_strs, p.path_mode);

    std::map<std::string, std::atomic<int>> op_tally;
    process_files(op_tally, files, p);

    if (p.verbose) {

	// Verify we did something on some file before we print anything
	bool did_op = false;
	for (const auto &kv : op_tally) {
	    if (kv.second.load(std::memory_order_relaxed) != 0) {
		did_op = true;
		break;
	    }
	}

	// If we did something interesting, report it - otherwise just note
	// that nothing happened.
	if (did_op) {
	    std::string cchar(1, p.clear_char);
	    if (p.clear_char == '\0')
		cchar = std::string("\\0");

	    std::cout << "Summary:\n";
	    std::cout << "    Original target string: " << target_str << "\n";
	    if (p.path_mode) {
		std::cout << "    Expanded path targets: \n";
		for (const auto &t : p.tgt_strs) {
		    if (t == target_str)
			continue;
		    std::cout << "                  : " << t << "\n";
		}
	    }
	    if (p.clear_char != '\0')
		std::cout << "            Clear char: " << cchar << "\n";
	    if (p.replace_str.length())
		std::cout << "    Replacement string: " << p.replace_str << "\n";

	    std::cout << "----------Processed Paths-------\n";

	    // print tally
	    for (const auto &kv : op_tally) {
		int v = kv.second.load(std::memory_order_relaxed);
		if (!v)
		    continue;
		std::cout << kv.first << ": ";
		if (v < 0) {
		    std::cout << " cleared " << -1*v << " instances\n";
		} else {
		    std::cout << "replaced " << v << " instances\n";
		}
	    }
	}
    }
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
