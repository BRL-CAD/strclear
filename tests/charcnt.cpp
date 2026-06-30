/*                     C H A R C N T . C P P
 * BRL-CAD
 *
 * Published in 2025 by the United States Government.
 * This work is in the public domain.
 */

#include <climits>
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>

int
main(int argc, const char **argv)
{
    if (argc != 2 && argc != 3) {
	std::cerr << "Usage: charcnt <filename> [char]\n";
	return -1;
    }

    char c = '\0';
    if (argc == 3) {
	if (strlen(argv[2]) != 1) {
	    std::cerr << "Error - second argument (if present) must be a single char\n";
	    return -1;
	} else {
	    c = argv[2][0];
	}
    }

    std::ifstream fs(argv[1], std::ios::binary);
    if (!fs.is_open()) {
	std::cerr << "Error: Could not open file " << argv[1] << "\n";
	return -1;
    }

    // Iterate, and count all the nulls
    unsigned long long nc = 0;
    char byte;
    while (fs.get(byte))
	nc = (byte == c) ? nc + 1 : nc;
    fs.close();

    std::string rstr;
   if (c == '\0') {
       rstr = std::string("null");
   } else {
       rstr = c;
   }

    std::cout << "Found " << nc << " " << rstr << " characters\n";
    if (nc > INT_MAX)
	std::cout << "Error - more than " << INT_MAX << " chars found!\n";

    return (int)nc;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
