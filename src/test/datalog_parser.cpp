#include "datalog_parser.h"
#include "ast_pp.h"
#include "arith_decl_plugin.h"
#include "dl_context.h"
#include "front_end_params.h"
#include "reg_decl_plugins.h"

using namespace datalog;


static void dparse_string(char const* str) {
    ast_manager m;
    front_end_params params;
    reg_decl_plugins(m);

    context ctx(m, params);
    parser* p = parser::create(ctx,m);

    bool res=p->parse_string(str);

    if (!res) {
        std::cout << "Parser did not succeed on string\n"<<str<<"\n";
        SASSERT(false);
    }
#if 0
    unsigned num_rules = p->get_num_rules();
    for (unsigned j = 0; j < num_rules; ++j) {
        rule* r = p->get_rules()[j];
        std::cout << mk_pp(r->head(), m) << "\n";
        for (unsigned i = 0; i < r->size(); ++i) {
            std::cout << "body: " << mk_pp((*r)[i], m) << "\n";
        }
    }
#endif
    dealloc(p);
}

static void dparse_file(char const* file) {
    ast_manager m;
    front_end_params params;
    reg_decl_plugins(m);

    context ctx(m, params);
    parser* p = parser::create(ctx,m);

    if (!p->parse_file(file)) {
        std::cout << "Failed to parse file\n";
    }
#if 0
    unsigned num_rules = p->get_num_rules();
    for (unsigned j = 0; j < num_rules; ++j) {
        rule* r = p->get_rules()[j];
        std::cout << mk_pp(r->head(), m) << "\n";
        for (unsigned i = 0; i < r->size(); ++i) {
            std::cout << "body: " << mk_pp((*r)[i], m) << "\n";
        }
    }
#endif
    dealloc(p);
}



void tst_datalog_parser() {
    dparse_string("\nH :- C1(X,a,b), C2(Y,a,X) .");
    dparse_string("N 128\n\nH :- C1(X,a,b), C2(Y,a,X) .");
    dparse_string("N 128\nI 128\n\nC1(x : N, y : N, z : I)\nC2(x : N, y : N, z : N)\nH :- C1(X,a,b), C2(Y,a,X) .");
    dparse_string("\nH :- C1(X,a,b), nC2(Y,a,X) .");
    dparse_string("\nH :- C1(X,a,b),nC2(Y,a,X).");
    dparse_string("\nH :- C1(X,a,b),\\\nC2(Y,a,X).");
    dparse_string("\nH :- C1(X,a\\,\\b), C2(Y,a,X) .");
}

void tst_datalog_parser_file(char** argv, int argc, int & i) {
    if (i + 1 < argc) {
        dparse_file(argv[i+1]);
        i++;
    }
}

