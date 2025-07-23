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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <set>
#include <string>
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
	std::set<std::string> tgt_strs;
	std::string replace_str;
};


inline std::vector<std::string>
expand_path_forms(const std::string &input) {
    namespace fs = std::filesystem;
    std::vector<std::string> forms;
    if (!input.length())
	return forms;
    forms.push_back(input); // Always include original

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
    return forms;
}

int
process_binary(std::map<std::string, int> &op_tally, const std::string &fname, process_opts &p)
{
    // Read binary contents
    std::ifstream input_fs;
    input_fs.open(fname, std::ios::binary);
    if (!input_fs.is_open()) {
	std::cerr << "Unable to open file " << fname << "\n";
	return -1;
    }
    std::vector<char> bin_contents(std::istreambuf_iterator<char>(input_fs), {});
    input_fs.close();

    // Process all target strings
    bool changed = false;
    std::set<std::string>::iterator t_it;
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
	    op_tally[fname]--;
	    changed = true;
	}
    }
    if (!changed)
	return 0;

    // If we changed the contents, write them back out
    std::ofstream output_fs;
    output_fs.open(fname, std::ios::binary);
    if (!output_fs.is_open()) {
	std::cerr << "Unable to write updated file contents for " << fname << "\n";
	exit(-1);
    }

    std::copy(bin_contents.begin(), bin_contents.end(), std::ostreambuf_iterator<char>(output_fs));
    output_fs.close();

    return 0;
}

int
process_text(std::map<std::string, int> &op_tally, const std::string &fname, process_opts &p)
{
    // Read text contents
    std::ifstream input_fs(fname);
    if (!input_fs.is_open()) {
	std::cerr << "Unable to open file " << fname << "\n";
	return -1;
    }
    std::stringstream fbuffer;
    fbuffer << input_fs.rdbuf();
    std::string nfile_contents = fbuffer.str();
    input_fs.close();
    if (!nfile_contents.length())
	return 0;

    // For replace ops we count by incrementing and clear opts we count by
    // decrementing, so we know what to print in the tally.  A file is only
    // cleared or replaced, not both, and which it is depends on the
    // replacement string.
    int rincr = (p.replace_str.length()) ? 1 : -1;

    // Use index and std::string::find for O(N) replacement
    bool changed = false;
    std::set<std::string>::iterator t_it;
    for (t_it = p.tgt_strs.begin(); t_it != p.tgt_strs.end(); ++t_it) {
	size_t pos = 0;
	while ((pos = nfile_contents.find(*t_it, pos)) != std::string::npos) {
	    nfile_contents.replace(pos, t_it->size(), p.replace_str);
	    pos += p.replace_str.size();
	    op_tally[fname] += rincr;
	    changed = true;
	}
    }
    if (!changed)
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
    return 0;
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
process_files(std::map<std::string, int> &op_tally, std::set<std::string> &files, process_opts &p)
{
    if (!p.tgt_strs.size())
	return;

    std::set<std::string>::iterator f_it;
    for (f_it = files.begin(); f_it != files.end(); ++f_it) {
	std::ifstream check_fs(*f_it, std::ios::binary);
	if (!check_fs.is_open()) {
	    std::cerr << "Error:  unable to open " << *f_it << "\n";
	    exit(-1);
	}
	bool binary_mode = is_binary(check_fs);
	check_fs.close();

	if (binary_mode) {
	    process_binary(op_tally, *f_it, p);
	    continue;
	}

	process_text(op_tally, *f_it, p);
    }
}

int
main(int argc, const char *argv[])
{
    process_opts p;
    bool binary_only = false;
    bool binary_test_mode = false;
    bool path_mode = false;
    bool text_only = false;
    bool verbose = false;
    char clear_char = '\0';
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
	    ("b,binary-only", "Skip inputs that are text files.", cxxopts::value<bool>(p.text_only))
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
    if (binary_only && text_only) {
	std::cerr << "Error:  can specify binary-only or text-only, not both.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }

    // If we're testing whether a file is binary, we only take one argument
    if (binary_test_mode && nonopts.size() != 1) {
	std::cerr << "Error:  -B accepts exactly one file path as input.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }

    // Everything else needs at least a filename or file list and a target string
    if ((file_list.length() && !binary_only) && (nonopts.size() != 1 && nonopts.size() != 2)) {
	std::cerr << "Error:  when using a file list we need a target string and (optionally) a replacement string.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((file_list.length() && binary_only) && (nonopts.size() != 1)) {
	std::cerr << "Error:  when using a file list and binary-only filtering we only accept a target string and (optionally) a --clear-char character - using a full replacement string isn't supported.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((!file_list.length() && !binary_only) && (nonopts.size() != 2 && nonopts.size() != 3)) {
	std::cerr << "Error:  we need a file, a target string and (optionally) a replacement string.\n";
	std::cout << options.help({""}) << std::endl;
	return -1;
    }
    if ((!file_list.length() && binary_only) && (nonopts.size() != 2)) {
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

    std::set<std::string> tgt_strs;
    if (path_mode) {
	auto expanded = expand_path_forms(target_str);
	p.tgt_strs.insert(expanded.begin(), expanded.end());
    } else {
	p.tgt_strs.insert(target_str);
    }

    std::map<std::string, int> op_tally;
    process_files(op_tally, files, p);

    if (p.verbose) {
	if (op_tally.size()) {
	    std::string cchar(1, clear_char);
	    if (clear_char == '\0')
		cchar = std::string("\\0");

	    std::cout << "Summary:\n";
	    std::cout << "    Original target string: " << target_str << "\n";
	    if (path_mode) {
		std::cout << "    Expanded path targets: \n";
		std::set<std::string>::iterator t_it;
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
	    std::map<std::string, int>::iterator o_it;
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

