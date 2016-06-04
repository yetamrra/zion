#include <stdlib.h>
#include <fstream>
#include "lexer.h"
#include "logger_decls.h"
#include "logger.h"
#include "tests.h"
#include "llvm_utils.h"
#include "compiler.h"
#include "disk.h"

int main(int argc, char *argv[]) {
	ptr<logger> logger(make_ptr<standard_logger>(debug_else("", "zion.log"), "."));
    std::string cmd;
	if (argc >= 2) {
		cmd = argv[1];
	} else {
		log(log_error, "available commands: test, read-ir, compile, compile-modules, bc, run");
		return -1;
	}

	if (cmd == "test") {
		std::string filter = (argc == 3 ? argv[2] : "");
		if (getenv("T")) {
			filter = getenv("T");
		}

		if (run_tests(filter)) {
			return EXIT_SUCCESS;
		} else {
			return EXIT_FAILURE;
		}
	} else if (argc >= 3) {
		status_t status;
		compiler compiler(argv[2], {".", "lib", "tests"});

		if (cmd == "read-ir") {
				compiler.llvm_load_ir(status, argv[2]);
            if (!!status) {
                return EXIT_SUCCESS;
            } else {
                return EXIT_FAILURE;
            }
        } else if (cmd == "compile") {
			compiler.build(status);

			if (!!status) {
				return EXIT_SUCCESS;
			} else {
				return EXIT_FAILURE;
			}
        } else if (cmd == "compile-modules") {
			compiler.build(status);
			compiler.compile_modules(status);

			if (!!status) {
				return EXIT_SUCCESS;
			} else {
				return EXIT_FAILURE;
			}
        } else if (cmd == "bc") {
			compiler.build(status);

			if (!!status) {
				auto executable_filename = compiler.get_program_name();
				return compiler.emit_built_program(status, executable_filename);
			} else {
				return EXIT_FAILURE;
			}
        } else if (cmd == "run") {
			compiler.build(status);

			if (!!status) {
				auto executable_filename = compiler.get_program_name();
				int ret = compiler.emit_built_program(status, executable_filename);
				if (!!status) {
					return system(executable_filename.c_str());
				} else {
					return ret;
				}
			} else {
				return EXIT_FAILURE;
			}
		} else {
			panic(string_format("bad CLI invocation of %s", argv[0]));
		}
		
    }

	return EXIT_SUCCESS;
}
