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
 * If the -p option is specified and target_str is a filesystem path, the
 * target string is expanded into the various filesystem path forms and the
 * code attempts to replace (or clear) them all.
 */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
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


class process_opts {
    public:
	bool binary_only = false;
	bool binary_test_mode = false;
	bool path_mode = false;
	bool text_only = false;
	bool verbose = false;
	char clear_char = '\0';
	std::vector<std::string> tgt_strs;
	std::string replace_str;
};


inline std::vector<std::string>
expand_path_forms(const std::string &input) {
    namespace fs = std::filesystem;
    std::vector<std::string> forms;
    if (!input.length())
	return forms;

    // Always include original
    forms.push_back(input);

    try {
	fs::path p(input);
	if (fs::exists(p) && (fs::is_regular_file(p) || fs::is_symlink(p) || fs::is_directory(p))) {
	    // Absolute form
	    std::error_code ec;
	    auto abs = fs::absolute(p, ec).string();
	    if (!abs.empty() && abs != input) forms.push_back(abs);

	    // Canonical form (resolves symlinks, may fail if not accessible)
	    ec.clear();
	    auto canon = fs::canonical(p, ec).string();
	    if (!ec && !canon.empty() && canon != input && canon != abs) forms.push_back(canon);

	    // Normalized (lexically, does not resolve symlinks)
	    auto norm = p.lexically_normal().string();
	    if (!norm.empty() && norm != input && norm != abs && norm != canon) forms.push_back(norm);
	}
    } catch (...) {
	// Ignore errors (broken symlink, permission denied, etc).
    }

    // For replacement purposes, we need longest to shortest so shorter
    // paths don't match as subsets of longer ones and mess up processing
    std::sort(forms.begin(), forms.end(),
	        [](const std::string &a, const std::string &b) {
		   if (a.size() != b.size())
		      return a.size() > b.size(); // longer first
		   return a > b; // lexicographical descending
	        }
	     );

    return forms;
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
    std::vector<std::string>::iterator t_it;
    for (t_it = p.tgt_strs.begin(); t_it != p.tgt_strs.end(); ++t_it) {
	std::vector<char> search_chars(t_it->begin(), t_it->end());
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
    std::vector<std::string>::iterator t_it;
    for (t_it = p.tgt_strs.begin(); t_it != p.tgt_strs.end(); ++t_it) {
	size_t pos = 0;
	while ((pos = nfile_contents.find(*t_it, pos)) != std::string::npos) {
	    nfile_contents.replace(pos, t_it->size(), p.replace_str);
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

// Text vs. binary file heuristic (generated with GPT-4.1 assistance)
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
	// Accept printable ASCII (32–126), CR, LF, TAB, FF
	if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t' || c == '\f')
	    continue;
	// Accept valid 8-bit UTF-8 lead bytes (for text, this is not 100% but helps)
	if ((unsigned char)c >= 0xC2 && (unsigned char)c <= 0xF4)
	    continue;
	n_nontext++;
    }

    if (n_read == 0)
	return false; // empty file: treat as text

    // If more than 10% non-text, guess binary
    return (double)n_nontext / n_read > nontext_threshold;
}

void
process_files(std::map<std::string, std::atomic<int>> &op_tally, std::set<std::string> &files, process_opts &p)
{
    if (files.empty() || !p.tgt_strs.size())
        return;

    // Thread pool parameters
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0)
	num_threads = 4; // Fallback
    std::queue<std::string> work_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<bool> done(false);

    // Add all files to the work queue
    for (const auto& fname : files)
        work_queue.push(fname);

    // Worker lambda
    auto worker = [&]() {
        while (true) {
            std::string fname;
	    {
		std::unique_lock<std::mutex> lock(queue_mutex);
		cv.wait(lock, [&]() { return !work_queue.empty() || done.load(); });
		// If there is work, process it.
		if (!work_queue.empty()) {
		    fname = work_queue.front();
		    work_queue.pop();
		} else if (done.load()) {
		    // No work and done, so exit.
		    return;
		} else {
		    // Spurious wakeup, continue waiting.
		    continue;
		}
	    }

            std::ifstream check_fs(fname, std::ios::binary);
            if (!check_fs.is_open()) {
                std::cerr << "Error:  unable to open " << fname << "\n";
                op_tally[fname] = 0;
                continue;
            }
            bool binary_mode = is_binary(check_fs);
            check_fs.close();

            int result = 0;
	    if (binary_mode && !p.text_only)
		result = process_binary(fname, p);
	    if (!binary_mode && !p.binary_only)
		result = process_text(fname, p);
	    op_tally[fname] = result;
        }
    };

    // Launch thread pool
    std::vector<std::thread> threads;
    for (unsigned int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    // Notify all threads in case they're waiting on empty queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        done = true;
    }
    cv.notify_all();

    // Join threads
    for (auto &t : threads)
	t.join();
}

int
main(int argc, const char *argv[])
{
    process_opts p;
    std::string file_list;

    cxxopts::Options options(argv[0],
	    "A program to clear or replace strings in files.\n"
	    "\n"
	    "strclear -B <filename>\n"
	    "strclear <filename> <target_str> [replacement_str]\n"
	    "strclear -f <filelist> <target_str> [replacement_str]\n"
	    "\n"
	    "When the -p option is added, a target string supplied for clearing\n"
	    "or replacement appears to be a filesystem path (e.g., an existing file\n"
	    "or directory), this tool will automatically search for and operate on\n"
	    "all recognized forms of that path within the file. This includes:\n"
	    "  - the original path string as supplied\n"
	    "  - its absolute path form\n"
	    "  - its canonical (fully resolved, with symlinks removed) form\n"
	    "  - its normalized (syntactically simplified) form\n"
	    "This ensures that both relative and absolute references, as well as\n"
	    "symlinked and normalized forms of the same file, are detected and processed.\n"
	    );

    std::vector<std::string> nonopts;

    try
    {
	options
	    .set_width(70)
	    .add_options()
	    ("B,is_binary",   "Test the file to see if it is a binary file (return 1 if yes, 0 if no.)", cxxopts::value<bool>(p.binary_test_mode))
	    ("t,text-only",   "Skip inputs that are binary files.", cxxopts::value<bool>(p.text_only))
	    ("b,binary-only", "Skip inputs that are text files.", cxxopts::value<bool>(p.binary_only))
	    ("f,files",       "Provide a list of files to process.", cxxopts::value<std::string>(file_list))
	    ("clear_char",    "Specify a character to use when clearing strings in files", cxxopts::value<char>(p.clear_char))
	    ("p,paths",       "Expand a target string that is a file path into all recognized forms (original, absolute, canonical, normalized) for searching and replacing/clearing.", cxxopts::value<bool>(p.path_mode))
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
    }
    catch (const cxxopts::exceptions::exception& e)
    {
	std::cerr << "error parsing options: " << e.what() << std::endl;
	return -1;
    }

    /////////////////////////////////////////
    // Do some option checking and validation
    /////////////////////////////////////////


    // binary_only ∩ text_only == NULL set
    if (p.binary_only && p.text_only) {
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

    // Everything else needs at least a filename or file list and a target string
    if ((file_list.length() && !p.binary_only) && (nonopts.size() != 1 && nonopts.size() != 2)) {
	std::cerr << "Error:  when using a file list we need a target string and (optionally) a replacement string.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((file_list.length() && p.binary_only) && (nonopts.size() != 1)) {
	std::cerr << "Warning:  binary filtering uses a target string and (optionally) a --clear-char character - full replacement strings are not supported.  Ignoring specified replacement string.\n";
    }
    if ((!file_list.length() && !p.binary_only) && (nonopts.size() != 2 && nonopts.size() != 3)) {
	std::cerr << "Error:  we need a file, a target string and (optionally) a replacement string.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((!file_list.length() && p.binary_only) && (nonopts.size() != 2)) {
	std::cerr << "Error:  when in binary-only mode we only accept a filename, a target string and (optionally) a --clear-char character - using a full replacement string isn't supported.\n";
	return -1;
    }

    std::set<std::string> files;
    std::string target_str;
    if (!file_list.length()) {
	files.insert(nonopts[0]);
	target_str = nonopts[1];
	p.replace_str = (nonopts.size() > 2) ? std::string(nonopts[2]) : std::string("");
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
	p.replace_str = (nonopts.size() > 1) ? std::string(nonopts[1]) : std::string("");
    }

    if (!target_str.length()) {
	std::cerr << "Error: empty target string supplied\n";
	return -1;
    }

    std::vector<std::string> tgt_strs;
    if (p.path_mode) {
	p.tgt_strs = expand_path_forms(target_str);
    } else {
	p.tgt_strs.push_back(target_str);
    }

    std::map<std::string, std::atomic<int>> op_tally;
    process_files(op_tally, files, p);

    if (p.verbose) {

	// Verify we did something on some file before we print anything
	std::map<std::string, std::atomic<int>>::iterator o_it;
	bool did_op = false;
	for (o_it = op_tally.begin(); o_it != op_tally.end(); ++o_it) {
	    if (o_it->second != 0) {
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
		std::vector<std::string>::iterator t_it;
		for (t_it = p.tgt_strs.begin(); t_it != p.tgt_strs.end(); ++t_it) {
		    if (*t_it == target_str)
			continue;
		    std::cout << "                  : " << *t_it << "\n";
		}
	    }
	    if (p.clear_char != '\0')
		std::cout << "            Clear char: " << p.clear_char << "\n";
	    if (p.replace_str.length())
		std::cout << "    Replacement string: " << p.replace_str << "\n";

	    std::cout << "----------Processed Paths-------\n";

	    // print tally
	    for (o_it = op_tally.begin(); o_it != op_tally.end(); ++o_it) {
		std::cout << o_it->first << ": ";
		if (o_it->second < 0) {
		    std::cout << " cleared " << -1*o_it->second << " instances\n";
		} else {
		    std::cout << "replaced " << o_it->second << " instances\n";
		}
	    }
	} else {
	    std::cout << "No matches found\n";
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

