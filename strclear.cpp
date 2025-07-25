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
 * Two modes for this tool:
 *
 * Given a binary (or text) file and a string, replace any instances of the
 * string in the binary with null chars.
 *
 * Given a text file, a target string and a replacement string, replace all
 * instances of the target string with the replacement string.
 * TODO: For the moment, the replacement string can't contain the target string.
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

extern "C" char *
strnstr(const char *h, const char *n, size_t hlen);

inline std::vector<std::string> expand_path_forms(const std::string &input) {
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
process_binary(std::string &fname, std::vector<std::string> &target_strs, char clear_char, bool verbose, bool path_mode)
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

    std::set<std::string> tgt_strs;
    if (path_mode) {
	for (const auto& t : target_strs) {
	    auto expanded = expand_path_forms(t);
	    tgt_strs.insert(expanded.begin(), expanded.end());
	}
    } else {
	tgt_strs.insert(target_strs.begin(), target_strs.end());
    }

    // Set up vectors of target and array of null chars
    int grcnt = 0;
    std::set<std::string>::iterator t_it;
    for (t_it = tgt_strs.begin(); t_it != tgt_strs.end(); ++t_it) {
	std::vector<char> search_chars(t_it->begin(), t_it->end());
	std::vector<char> null_chars(search_chars.size(), clear_char);

	// Find instances of target string in binary, and replace any we find
	auto position = bin_contents.begin();
	int rcnt = 0;
	while ((position = std::search(position, bin_contents.end(), search_chars.begin(), search_chars.end())) != bin_contents.end()) {
	    std::copy(null_chars.begin(), null_chars.end(), position);
	    rcnt++;
	    if (verbose && rcnt == 1)
		std::cout << fname << ":\n";
	    if (verbose) {
		std::string cchar(1, clear_char);
		if (clear_char == '\0')
		    cchar = std::string("\\0");
		std::cout << "\tclearing instance #" << rcnt << " of " << *t_it << " with the '" << cchar << "' char\n";
	    }
	    position += search_chars.size();
	}
	grcnt += rcnt;
    }
    if (!grcnt)
	return 0;

    // If we changed the contents, write them back out
    std::ofstream output_fs;
    output_fs.open(fname, std::ios::binary);
    if (!output_fs.is_open()) {
	std::cerr << "Unable to write updated file contents for " << fname << "\n";
	return -1;
    }

    std::copy(bin_contents.begin(), bin_contents.end(), std::ostreambuf_iterator<char>(output_fs));
    output_fs.close();

    return 0;
}

int
process_text(std::string &fname, std::string &target_str, std::string &replace_str, bool verbose, bool path_mode)
{
    // Make sure the replacement doesn't contain the target.  If we need that
    // we'll have to be more sophisticated about the replacement logic, but
    // for now just be simple
    auto loop_check = std::search(replace_str.begin(), replace_str.end(), target_str.begin(), target_str.end());
    if (loop_check != replace_str.end()) {
	std::cerr << "Replacement string \"" << replace_str << "\" contains target string \"" << target_str << "\" - unsupported.\n";
	return -1;
    }

    std::set<std::string> tgt_strs;
    if (path_mode) {
	auto expanded = expand_path_forms(target_str);
	tgt_strs.insert(expanded.begin(), expanded.end());
    } else {
	tgt_strs.insert(target_str);
    }

    std::ifstream input_fs(fname);
    std::stringstream fbuffer;
    fbuffer << input_fs.rdbuf();
    std::string nfile_contents = fbuffer.str();
    input_fs.close();
    if (!nfile_contents.length())
	return 0;

    // Use index and std::string::find for O(N) replacement
    bool changed = false;
    std::set<std::string>::iterator t_it;
    for (t_it = tgt_strs.begin(); t_it != tgt_strs.end(); ++t_it) {
	size_t pos = 0;
	int rcnt = 0;
	while ((pos = nfile_contents.find(*t_it, pos)) != std::string::npos) {
	    nfile_contents.replace(pos, t_it->size(), replace_str);
	    rcnt++;
	    changed = true;
	    if (verbose && rcnt == 1)
		std::cout << fname << ":\n";
	    if (verbose)
		std::cout << "\treplacing instance #" << rcnt << " of " << *t_it << " with " << replace_str << "\n";
	    pos += replace_str.size();
	}
    }
    if (!changed)
	return 0;

    // If we changed the contents, write them back out
    std::ofstream output_fs;
    output_fs.open(fname, std::ios::trunc);
    if (!output_fs.is_open()) {
	std::cerr << "Unable to write updated file contents for " << fname << "\n";
	return -1;
    }
    output_fs << nfile_contents;
    output_fs.close();
    return 0;
}

// Text vs. binary file heuristic (generated with GPT-4.1 assistance)
bool is_binary(std::ifstream &file, size_t max_check = 4096, double nontext_threshold = 0.1)
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

int
main(int argc, const char *argv[])
{
    bool binary_mode = false;
    bool binary_test_mode = false;
    bool clear_mode = false;
    char clear_char = '\0';
    bool swap_mode = false;
    bool text_mode = false;
    bool path_mode = false;
    bool verbose = false;

    cxxopts::Options options(argv[0],
	    "A program to clear or replace strings in files.\n"
	    "\n"
	    "When the -p option is activated, a target string supplied for clearing\n"
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
	    ("B,is_binary","Test the file to see if it is a binary file.)", cxxopts::value<bool>(binary_test_mode))
	    ("b,binary",   "Treat the input file as binary.  (Note that only string clearing is supported with binary files.)", cxxopts::value<bool>(binary_mode))
	    ("c,clear",    "Replace strings in files by overwriting a specified character (defaults to NULL.)  Note: If the target string is a path and -p is specified, all equivalent path forms are processed.", cxxopts::value<bool>(clear_mode))
	    ("clear_char", "Specify a character to use when clearing strings in files", cxxopts::value<char>(clear_char))
	    ("r,replace",  "Replace one string with another (text mode only). Note: If the target string is a path and -p is specified, all equivalent path forms are processed.", cxxopts::value<bool>(swap_mode))
	    ("p,paths",    "Expand a target string that is a file path into all recognized forms (original, absolute, canonical, normalized) for searching and replacing/clearing.", cxxopts::value<bool>(path_mode))
	    ("t,text",     "Refuse to run unless the input file is a text file.", cxxopts::value<bool>(text_mode))
	    ("v,verbose",  "Verbose reporting during processing", cxxopts::value<bool>(verbose))
	    ("h,help",     "Print help")
	    ;
	auto result = options.parse(argc, argv);

	nonopts = result.unmatched();

	if (result.count("help")) {
	    std::cout << options.help({""}) << std::endl;
	    return 0;
	}

	if (binary_mode && text_mode) {
	    std::cerr << "Error:  need to specify either binary or text mode, not both\n";
	    return -1;
	}

	if (!clear_mode && !swap_mode && !binary_test_mode) {
	    std::cerr << "Error:  need to specify either clear mode (-c), replace mode (-r), or binary file test mode (-B)\n";
	    return -1;
	}

	if (clear_mode && swap_mode) {
	    std::cerr << "Error:  need to specify either clear or replace mode, not both\n";
	    return -1;
	}
    }
    catch (const cxxopts::exceptions::exception& e)
    {
	std::cerr << "error parsing options: " << e.what() << std::endl;
	return -1;
    }

    // Unless the goal is strictly to test file type, we need at least two args
    if (nonopts.size() < 2 && !binary_test_mode) {
	std::cout << options.help({""}) << std::endl;
	return -1;
    }

    // If we only have a filename and a single string, the only thing we can
    // do is treat the file as binary and replace the string
    if (nonopts.size() == 2 && swap_mode) {
	std::cerr << "Error:  string replacement mode indicated, but no replacement specified\n";
	return -1;
    }

    // For swap_mode, we need exactly 3 nonopts (file, target, replace)
    if (swap_mode && nonopts.size() != 3) {
       std::cerr << "Error:  replacing string in text file - need file, target string and replacement string as arguments.\n";
       return -1;
    }

    std::string fname(nonopts[0]);

    // Determine if the file is a binary or text file, if we've not been told
    // to treat it as binary explicitly with -b.  If we've been told text mode
    // we still check to make sure we really have a text file before processing.
    if (!binary_mode) {
	std::ifstream check_fs(fname, std::ios::binary);
	if (!check_fs.is_open()) {
	    std::cerr << "Error:  unable to open " << fname << "\n";
	    return -1;
	}
	binary_mode = is_binary(check_fs);
    }

    // If all we're supposed to do is determine the type, return success (0) if
    // the file is binary, else 1
    if (binary_test_mode)
	return (binary_mode) ? 0 : 1;

    if (binary_mode && swap_mode) {
	std::cerr << "Error:  string replacement indicated, but file is binary\n";
	return -1;
    }

    // If we're in binary mode we're just nulling out the target string(s).
    if (binary_mode) {
	std::vector<std::string> target_strs;
	for (size_t i = 1; i < nonopts.size(); i++) {
	    if (nonopts[i].length())
		target_strs.push_back(nonopts[i]);
	}
	if (!target_strs.size()) {
	    std::cerr << "Target strings cannot be empty.\n";
	    return -1;
	}
	return process_binary(fname, target_strs, clear_char, verbose, path_mode);
    }

    std::string target_str(nonopts[1]);
    if (!target_str.length()) {
	std::cerr << "Target string cannot be empty.\n";
	return -1;
    }
    std::string replace_str = (swap_mode) ? std::string(nonopts[2]) : std::string("");
    return process_text(fname, target_str, replace_str, verbose, path_mode);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8

