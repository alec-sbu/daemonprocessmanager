/**
 * === DO NOT MODIFY THIS FILE ===
 * If you need some other prototypes or constants in a header, please put them
 * in another header file.
 *
 * When we grade, we will be replacing this file with our own copy.
 * You have been warned.
 * === DO NOT MODIFY THIS FILE ===
 */

#include <stdlib.h>

#include "legion.h"

extern int sf_manual_mode;

int main(int argc, char const *argv[]) {
    if(argc == 1)
	sf_manual_mode = 1;
    FILE* test = fopen("testfile","r");
    sf_init();
    run_cli(stdin, stdout);
    sf_fini();
    fclose(test);
    return EXIT_SUCCESS;
}
