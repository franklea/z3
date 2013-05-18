#include <string>
#include <list>
#include <vector>
#include <set>
#include <map>
#include <signal.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include "z3++.h"

struct region {
    std::list<void*> m_alloc;

    void * allocate(size_t s) {
        void * res = new char[s];
        m_alloc.push_back(res);
        return res;
    }

    ~region() {
        std::list<void*>::iterator it = m_alloc.begin(), end = m_alloc.end();
        for (; it != end; ++it) {
            delete *it;
        }
    }    
};

template<typename T>
class flet {
    T & m_ref;
    T   m_old;
public:
    flet(T& x, T const& y): m_ref(x), m_old(x) { x = y; }
    ~flet() { m_ref = m_old; }
};

struct symbol_compare {
    bool operator()(z3::symbol const& s1, z3::symbol const& s2) const {
        return s1 < s2;
    };
};


template <typename T>
struct symbol_table {
    std::map<z3::symbol, T> m_map;

    void insert(z3::symbol const& s, T& val) {
        m_map.insert(std::pair<z3::symbol const, T>(s, val));
    }

    bool find(z3::symbol const& s, T& val) { 
        std::map<z3::symbol, T>::iterator it = m_map.find(s);
        if (it == m_map.end()) {
            return false;
        }
        else {
            val = it->second;
            return true;
        }
    }
    
};



typedef std::set<z3::symbol, symbol_compare> symbol_set;

struct named_formulas {
    std::vector<std::pair<z3::expr, char const *> > m_formulas;
    bool m_has_conjecture;

    named_formulas(): m_has_conjecture(false) {}

    void push_back(z3::expr& fml, char const * name) {
        m_formulas.push_back(std::make_pair(fml, name));
    }

    void set_has_conjecture() {
        m_has_conjecture = true;
    }

    bool has_conjecture() const {
        return m_has_conjecture;
    }
};

inline void * operator new(size_t s, region & r) { return r.allocate(s); }

inline void * operator new[](size_t s, region & r) { return r.allocate(s); }

inline void operator delete(void *, region & ) { /* do nothing */ }

inline void operator delete[](void *, region & ) { /* do nothing */ }



extern char* tptp_lval[];
extern int yylex();

static char* strdup(region& r, char const* s) {
    size_t l = strlen(s) + 1;
    char* result = new (r) char[l];
    memcpy(result, s, l);
    return result;
}

class TreeNode {
    char const* m_symbol;
    int         m_symbol_index;
    TreeNode**  m_children;
    
public:
    TreeNode(region& r, char const* sym, 
             TreeNode* A, TreeNode* B, TreeNode* C, TreeNode* D, TreeNode* E,
             TreeNode* F, TreeNode* G, TreeNode* H, TreeNode* I, TreeNode* J):
        m_symbol(strdup(r, sym)),
        m_symbol_index(-1) {
        m_children = new (r) TreeNode*[10];
        m_children[0] = A;
        m_children[1] = B;
        m_children[2] = C;
        m_children[3] = D;
        m_children[4] = E;
        m_children[5] = F;
        m_children[6] = G;
        m_children[7] = H;
        m_children[8] = I;
        m_children[9] = J;

    }
        
    char const* symbol() const { return m_symbol; }
    TreeNode *const* children() const { return m_children; }
    TreeNode* child(unsigned i) const { return m_children[i]; }
    int index() const { return m_symbol_index; }

    void set_index(int idx) { m_symbol_index = idx; }
};

TreeNode* MkToken(region& r, char* token, int symbolIndex) { 
    TreeNode* ss;
    char* symbol = tptp_lval[symbolIndex];
    ss = new (r) TreeNode(r, symbol, NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
    ss->set_index(symbolIndex);
    return ss; 
}


// ------------------------------------------------------
// Build Z3 formulas.

class env {
    z3::context&                  m_context;
    z3::expr_vector               m_bound;  // vector of bound constants.
    z3::sort                       m_univ;
    symbol_table<z3::func_decl>    m_decls;
    symbol_table<z3::func_decl>    m_decls2;
    symbol_table<z3::sort>         m_defined_sorts;
    static std::vector<TreeNode*>*  m_nodes;    
    static region*                m_region;
    char const*                   m_filename;


    enum binary_connective {
        IFF,
        IMPLIES,
        IMPLIED,
        LESS_TILDE_GREATER,
        TILDE_VLINE        
    };

    bool mk_error(TreeNode* f, char const* msg) {
        std::cerr << "expected: " << msg << "\n";
        std::cerr << "got: " << f->symbol() << "\n";
        return false;
    }

    bool mk_input(TreeNode* f, named_formulas& fmls) {
        if (!strcmp(f->symbol(),"annotated_formula")) {
            return mk_annotated_formula(f->child(0), fmls);
        }
        if (!strcmp(f->symbol(),"include")) {
            return mk_include(f->child(2), f->child(3), fmls);
        }
        return mk_error(f, "annotated formula or include");
    }

    bool mk_annotated_formula(TreeNode* f, named_formulas& fmls) {
        if (!strcmp(f->symbol(),"fof_annotated")) {
            return fof_annotated(f->child(2), f->child(4), f->child(6), f->child(7), fmls);            
        }
        if (!strcmp(f->symbol(),"tff_annotated")) {
            return fof_annotated(f->child(2), f->child(4), f->child(6), f->child(7), fmls);            
        }
        if (!strcmp(f->symbol(),"cnf_annotated")) {
            return cnf_annotated(f->child(2), f->child(4), f->child(6), f->child(7), fmls);
        }
        if (!strcmp(f->symbol(),"thf_annotated")) {
            return mk_error(f, "annotated formula (not thf)");
        }
        return mk_error(f, "annotated formula");
    }

    bool mk_include(TreeNode* file_name, TreeNode* formula_selection, named_formulas& fmls) {
        char const* fn = file_name->child(0)->symbol();
        TreeNode* name_list = formula_selection->child(2);
        if (name_list && !strcmp("null",name_list->symbol())) {
            
            name_list = 0;
        }
        std::string inc_name;
        bool f_exists = false;
        for (unsigned i = 1; !f_exists && i <= 3; ++i) {
            f_exists = mk_filename(fn, i, inc_name);
        }
        if (!f_exists) {
            f_exists = mk_env_filename(fn, inc_name);            
        }
        
        if (!parse(inc_name.c_str(), fmls)) {
            return false;
        }
        while (name_list) {
            return mk_error(name_list, "name list (not handled)");
            char const* name = name_list->child(0)->symbol();
            name_list = name_list->child(2);            
        }
        return true;
    }

#define CHECK(_node_) if (0 != strcmp(_node_->symbol(),#_node_)) return mk_error(_node_,#_node_); 

    const char* get_name(TreeNode* name) {
        return name->child(0)->child(0)->symbol();        
    }

    z3::expr mk_forall(z3::expr_vector& bound, z3::expr body) {
        return mk_quantifier(true, bound, body);
    }

    z3::expr mk_quantifier(bool is_forall, z3::expr_vector& bound, z3::expr body) {
        Z3_app* vars = new Z3_app[bound.size()];
        for (unsigned i = 0; i < bound.size(); ++i) {
            vars[i] = (Z3_app) bound[i];
        }
        Z3_ast r = Z3_mk_quantifier_const(m_context, true, 1, bound.size(), vars, 0, 0, body);
        delete[] vars;
        return z3::expr(m_context, r);
    }

    bool cnf_annotated(TreeNode* name, TreeNode* formula_role, TreeNode* formula, TreeNode* annotations, named_formulas& fmls) {
        symbol_set st;
        get_cnf_variables(formula, st);
        symbol_set::iterator it = st.begin(), end = st.end();
        std::vector<z3::symbol>  names;
        for(; it != end; ++it) {
            names.push_back(*it);
            m_bound.push_back(m_context.constant(names.back(), m_univ));
        }
        z3::expr r(m_context);
        bool ok = cnf_formula(formula, r);
        if (ok && !m_bound.empty()) {
            r = mk_forall(m_bound, r);
        }
        char const* role = formula_role->child(0)->symbol();
        if (ok && !strcmp(role,"conjecture")) {
            fmls.set_has_conjecture();
            r = !r;
        }
        if (ok) {
            fmls.push_back(r, get_name(name));
        }
        m_bound.resize(0);
        return ok;        
    }

    bool cnf_formula(TreeNode* formula, z3::expr& r) {
        std::vector<z3::expr> disj;
        bool ok = true;
        if (formula->child(1)) {
            ok = disjunction(formula->child(1), disj);
        }
        else {
            ok = disjunction(formula->child(0), disj);
        }
        if (ok) {
            if (disj.size() > 0) {
                r = disj[0];
            }
            else {
                r = m_context.bool_val(false);
            }
            for (unsigned i = 1; i < disj.size(); ++i) {
                r = r || disj[i];
            }
        }
        return ok;
    }

    bool disjunction(TreeNode* d, std::vector<z3::expr>& r) {
        z3::expr lit(m_context);
        if (d->child(2)) {
            if(!disjunction(d->child(0), r)) return false;
            if(!literal(d->child(2), lit)) return false;
            r.push_back(lit);
            return true;            
        }
        if(!literal(d->child(0), lit)) return false;
        r.push_back(lit);
        return true;
    }

    bool literal(TreeNode* l, z3::expr& lit) {
        if (!strcmp(l->child(0)->symbol(),"~")) {
            if (!fof_formula(l->child(1), lit)) return false;
            lit = !lit;
            return true;
        }
        return fof_formula(l->child(0), lit);
    }

    bool fof_annotated(TreeNode* name, TreeNode* formula_role, TreeNode* formula, TreeNode* annotations, named_formulas& fmls) {
        z3::expr fml(m_context);
        //CHECK(fof_formula);
        CHECK(formula_role);
        if (!fof_formula(formula->child(0), fml)) {
            return false;
        }
        char const* role = formula_role->child(0)->symbol();
        if (!strcmp(role,"conjecture")) {
            fmls.set_has_conjecture();
            fmls.push_back(!fml, get_name(name));
        }
        else if (!strcmp(role,"type")) {
        }
        else {
            fmls.push_back(fml, get_name(name));
        }
        return true;
    }

    bool fof_formula(TreeNode* f, z3::expr& fml) {
        z3::expr f1(m_context);
        char const* name = f->symbol();
        if (!strcmp(name,"fof_logic_formula") ||
            !strcmp(name,"fof_binary_assoc") ||
            !strcmp(name,"fof_binary_formula") ||
            !strcmp(name,"tff_logic_formula") ||
            !strcmp(name,"tff_binary_assoc") ||
            !strcmp(name,"tff_binary_formula") ||
            !strcmp(name,"atomic_formula") ||
            !strcmp(name,"defined_atomic_formula")) {
            return fof_formula(f->child(0), fml);
        }
        if (!strcmp(name, "fof_sequent") ||
            !strcmp(name, "tff_sequent")) {
            if (!fof_formula(f->child(0), f1)) return false;
            if (!fof_formula(f->child(2), fml)) return false;
            fml = implies(f1, fml);
            return true;
        }
        if (!strcmp(name, "fof_binary_nonassoc") ||
            !strcmp(name, "tff_binary_nonassoc")) {
            if (!fof_formula(f->child(0), f1)) return false;
            if (!fof_formula(f->child(2), fml)) return false;   
            //SASSERT(!strcmp("binary_connective",f->child(1)->symbol()));
            char const* conn = f->child(1)->child(0)->symbol();
            if (!strcmp(conn, "<=>")) {
                fml = (f1 == fml);
                return true;
            }
            if (!strcmp(conn, "=>")) {
                fml = implies(f1, fml);
                return true;
            }
            if (!strcmp(conn, "<=")) {
                fml = implies(fml, f1);
                return true;
            }
            if (!strcmp(conn, "<~>")) {
                fml = ! (f1 == fml);
                return true;
            }
            if (!strcmp(conn, "~|")) {
                fml = !(f1 || fml);
                return true;
            }
            if (!strcmp(conn, "~&")) {
                fml = ! (f1 && fml);
                return true;
            }
            return mk_error(f->child(1)->child(0), "connective");            
        }
        if (!strcmp(name,"fof_or_formula") ||
            !strcmp(name,"tff_or_formula")) {
            if (!fof_formula(f->child(0), f1)) return false;
            if (!fof_formula(f->child(2), fml)) return false;   
            fml = f1 || fml;
            return true;
        }
        if (!strcmp(name,"fof_and_formula") ||
            !strcmp(name,"tff_and_formula")) {
            if (!fof_formula(f->child(0), f1)) return false;
            if (!fof_formula(f->child(2), fml)) return false;   
            fml = f1 && fml;
            return true;
        }
        if (!strcmp(name,"fof_unitary_formula") ||
            !strcmp(name,"tff_unitary_formula")) {
            if (f->child(1)) {
                // parenthesis
                return fof_formula(f->child(1), fml);
            }
            return fof_formula(f->child(0), fml);
        }
        if (!strcmp(name,"fof_quantified_formula") ||
            !strcmp(name,"tff_quantified_formula")) {
            return fof_quantified_formula(f->child(0), f->child(2), f->child(5), fml);
        }

        if (!strcmp(name,"fof_unary_formula") ||
            !strcmp(name,"tff_unary_formula")) {
            if (!f->child(1)) {
                return fof_formula(f->child(0), fml);
            }
            if (!fof_formula(f->child(1), fml)) return false;
            char const* conn = f->child(0)->child(0)->symbol();
            if (!strcmp(conn,"~")) {
                fml = !fml;
                return true;
            }
            return mk_error(f->child(0)->child(0), "fof_unary_formula");
        }
        if (!strcmp(name,"fof_let")) {
            return mk_let(f->child(2), f->child(5), fml);
        }
        if (!strcmp(name,"variable")) {
            char const* v  = f->child(0)->symbol();
            if (find_bound(v, fml)) {
                return true;
            }
            return mk_error(f->child(0), "variable");
        }
        if (!strcmp(name,"fof_conditional")) {
            z3::expr f2(m_context);
            if (!fof_formula(f->child(2), f1)) return false;
            if (!fof_formula(f->child(4), f2)) return false;
            if (!fof_formula(f->child(6), fml)) return false;
            fml = ite(f1, f2, fml);
            return true;
        }
        if (!strcmp(name,"plain_atomic_formula") ||             
            !strcmp(name,"defined_plain_formula") ||
            !strcmp(name,"system_atomic_formula")) {
            return term(f->child(0), m_context.bool_sort(), fml);
        }
        if (!strcmp(name,"defined_infix_formula") ||
            !strcmp(name,"fol_infix_unary")) {
            z3::expr t1(m_context), t2(m_context);
            if (!term(f->child(0), m_univ, t1)) return false;
            if (!term(f->child(2), m_univ, t2)) return false;
            TreeNode* inf = f->child(1);
            while (inf && strcmp(inf->symbol(),"=") && strcmp(inf->symbol(),"!=")) {
                inf = inf->child(0);
            }
            if (!inf) {
                return mk_error(f->child(1), "defined_infix_formula");
            }
            char const* conn = inf->symbol();
            if (!strcmp(conn,"=")) {
                fml = t1 == t2;
                return true;
            }
            if (!strcmp(conn,"!=")) {
                fml = ! (t1 == t2);
                return true;
            }
            return mk_error(inf, "defined_infix_formula");
        }
        if (!strcmp(name, "tff_typed_atom")) {
            while (!strcmp(f->child(0)->symbol(),"(")) {
                f = f->child(1);
            }
            char const* id = 0;
            z3::sort s(m_context);
            z3::sort_vector sorts(m_context);

            if (!mk_id(f->child(0), id)) {
                return false;
            }
            if (is_ttype(f->child(2))) {
                mk_sort(id);
                m_defined_sorts.insert(symbol(id), s);
                return true;
            }
            if (!mk_mapping_sort(f->child(2), sorts, s)) return false;
            z3::func_decl fd(m_context.function(id, sorts, s));
            m_decls.insert(symbol(id), fd);
            return true;
        }
        return mk_error(f, "fof_formula");
    }

    bool is_ttype(TreeNode* t) {
        char const* name = t->symbol();
        if (!strcmp(name,"atomic_defined_word")) {
            return !strcmp("$tType", t->child(0)->symbol());
        }
        return false;
    }

    bool fof_quantified_formula(TreeNode* fol_quantifier, TreeNode* vl, TreeNode* formula, z3::expr& fml) {
        unsigned l = m_bound.size();       
        if (!mk_variable_list(vl)) return false;
        if (!fof_formula(formula, fml)) return false;
        bool is_forall = !strcmp(fol_quantifier->child(0)->symbol(),"!");
        z3::expr_vector bound(m_context);
        for (unsigned i = l; i < m_bound.size(); ++i) {
            bound.push_back(m_bound[i]);
        }
        fml = mk_quantifier(is_forall, bound, fml);
        m_bound.resize(l);
        return true;
    }

    bool mk_variable_list(TreeNode* variable_list) {
        while (variable_list) {
            TreeNode* var = variable_list->child(0);
            if (!strcmp(var->symbol(),"tff_variable")) {
                var = var->child(0);
            }
            if (!strcmp(var->symbol(),"variable")) {
                char const* name = var->child(0)->symbol();
                m_bound.push_back(m_context.constant(name, m_univ));
            }
            else if (!strcmp(var->symbol(),"tff_typed_variable")) {
                z3::sort s(m_context);
                char const* name = var->child(0)->child(0)->symbol();
                if (!mk_sort(var->child(2), s)) return false;
                m_bound.push_back(m_context.constant(name, s));
            }
            else {
                return mk_error(var, "variable_list");
            }
            variable_list = variable_list->child(2);
        }
        return true;
    }

    bool mk_sort(TreeNode* t, z3::sort& s) {
        char const* name = t->symbol();
        if (!strcmp(name, "tff_atomic_type") ||
            !strcmp(name, "defined_type")) {
            return mk_sort(t->child(0), s);
        }
        if (!strcmp(name, "atomic_defined_word")) {
            z3::symbol sname = symbol(t->child(0)->symbol());
            z3::sort srt(m_context);
            if (m_defined_sorts.find(sname, srt)) {
                s = srt;
            }
            else {
                s = mk_sort(sname);
                if (sname == symbol("$rat")) {
                    std::cerr << "rational sorts are not handled\n";
                    return false;
                }
                return mk_error(t, "defined sort");
            }
            return true;
        }
        if (!strcmp(name,"atomic_word")) {
            name = t->child(0)->symbol();
            s = mk_sort(symbol(name));
            return true;
        }
        return mk_error(t, "sort");
    }

    bool mk_mapping_sort(TreeNode* t, z3::sort_vector& domain, z3::sort& s) {
        char const* name = t->symbol();
        if (!strcmp(name,"tff_top_level_type")) {
            return mk_mapping_sort(t->child(0), domain, s);
        }
        if (!strcmp(name,"tff_atomic_type")) {
            return mk_sort(t->child(0), s);
        }
            
        if (!strcmp(name,"tff_mapping_type")) {
            TreeNode* t1 = t->child(0);
            if (t1->child(1)) {
                if (!mk_xprod_sort(t1->child(1), domain)) return false;
            }
            else {
                if (!mk_sort(t1->child(0), s)) return false;
                domain.push_back(s);
            }
            if (!mk_sort(t->child(2), s)) return false;
            return true;
        }
        return mk_error(t, "mapping sort");
    }

    bool mk_xprod_sort(TreeNode* t, z3::sort_vector& sorts) {        
        char const* name = t->symbol();
        z3::sort s1(m_context), s2(m_context);
        if (!strcmp(name, "tff_atomic_type")) {
            if (!mk_sort(t->child(0), s1)) return false;
            sorts.push_back(s1);
            return true;
        }

        if (!strcmp(name, "tff_xprod_type")) {
            name = t->child(0)->symbol();
            if (!strcmp(name, "tff_atomic_type") ||
                !strcmp(name, "tff_xprod_type")) {
                if (!mk_xprod_sort(t->child(0), sorts)) return false;
                if (!mk_xprod_sort(t->child(2), sorts)) return false;
                return true;
            }
            if (t->child(1)) {
                return mk_xprod_sort(t->child(1), sorts);
            }
        }
        return mk_error(t, "xprod sort");
    }

    bool term(TreeNode* t, z3::sort& s, z3::expr& r) {
        char const* name = t->symbol();
        if (!strcmp(name, "defined_plain_term") ||
            !strcmp(name, "system_term") ||
            !strcmp(name, "plain_term")) {
            if (!t->child(1)) {
                return term(t->child(0), s, r);
            }
            return apply_term(t->child(0), t->child(2), s, r);
        }
        if (!strcmp(name, "constant") ||
            !strcmp(name, "functor") ||
            !strcmp(name, "defined_plain_formula") ||
            !strcmp(name, "defined_functor") ||
            !strcmp(name, "defined_constant") ||
            !strcmp(name, "system_constant") ||
            !strcmp(name, "defined_atomic_term") ||
            !strcmp(name, "system_functor") ||
            !strcmp(name, "function_term") ||
            !strcmp(name, "term") ||
            !strcmp(name, "defined_term")) {
            return term(t->child(0), s, r);
        }


        if (!strcmp(name, "defined_atom")) {
            char const* name0 = t->child(0)->symbol();
            if (!strcmp(name0,"number")) {
                name0 = t->child(0)->child(0)->symbol();
                char const* per = strchr(name0, '.');
                bool is_real = 0 != per;
                bool is_rat = 0 != strchr(name0, '/');
                bool is_int = !is_real && !is_rat;
                if (is_int) {
                    r = m_context.int_val(name0);
                }
                else if (is_rat) {
                    r = m_context.real_val(name0);
                }
                else {
                    r = m_context.real_val(name0);
                    z3::expr y = m_context.real_val(per-name0);
                    r = r/y;
                }
                return true;
            }
            if (!strcmp(name0, "distinct_object")) {
                return false;
            }
            return mk_error(t->child(0), "number or distinct object");
        }
        if (!strcmp(name, "atomic_defined_word")) {
            char const* ch = t->child(0)->symbol();
            z3::symbol s = symbol(ch);
            z3::func_decl fd(m_context);
            if (!strcmp(ch, "$true")) {
                r = m_context.bool_val(true);
            }
            else if (!strcmp(ch, "$false")) {
                r = m_context.bool_val(false);
            }
            else if (m_decls.find(s, fd)) {
                r = fd(0,0);
                return true;
            }
            return mk_error(t->child(0), "atomic_defined_word");
        }
        if (!strcmp(name, "atomic_word")) {
            z3::func_decl f(m_context);
            z3::symbol sym = symbol(t->child(0)->symbol());
            if (m_decls.find(sym, f)) {
                r = f(0,0);
            }
            else {
                r = m_context.constant(sym, s);
            }
            return true;
        }
        if (!strcmp(name, "variable")) {
            char const* v = t->child(0)->symbol();
            if (find_bound(v, r)) {
                return true;
            }
            return mk_error(t->child(0), "variable not bound");
        }
        return mk_error(t, "term not recognized");
    }

    bool apply_term(TreeNode* f, TreeNode* args, z3::sort& s, z3::expr& r) {
        z3::expr_vector terms(m_context);
        z3::sort_vector sorts(m_context);
        if (!mk_args(args, terms)) return false;
        for (unsigned i = 0; i < terms.size(); ++i) {
            sorts.push_back(terms[i].get_sort());
        }
        if (!strcmp(f->symbol(),"functor") ||
            !strcmp(f->symbol(),"system_functor") ||
            !strcmp(f->symbol(),"defined_functor")) {
            f = f->child(0);
        }
        bool atomic_word = !strcmp(f->symbol(),"atomic_word");
        if (atomic_word ||
            !strcmp(f->symbol(),"atomic_defined_word") ||
            !strcmp(f->symbol(),"atomic_system_word")) {
            char const* ch = f->child(0)->symbol();
            z3::symbol fn = symbol(ch);   
            z3::func_decl fun(m_context);
            if (!strcmp(ch,"$less")) {
                if (terms.size() != 2) return false;
                r = terms[0] < terms[1]; 
            }
            else if (!strcmp(ch,"$lesseq")) {
                if (terms.size() != 2) return false;
                r = terms[0] <= terms[1];
            }
            else if (!strcmp(ch,"$greater")) {
                if (terms.size() != 2) return false;
                r = terms[0] > terms[1];
            }
            else if (!strcmp(ch,"$greatereq")) {
                if (terms.size() != 2) return false;
                r = terms[0] >= terms[1];
            }
            else if (!strcmp(ch,"$uminus")) {
                if (terms.size() != 1) return false;
                r = -terms[0];
            }
            else if (!strcmp(ch,"$sum")) {
                if (terms.size() != 2) return false;
                r = terms[0] + terms[1];
            }
            else if (!strcmp(ch,"$plus")) {
                if (terms.size() != 2) return false;
                r = terms[0] + terms[1];
            }
            else if (!strcmp(ch,"$difference")) {
                if (terms.size() != 2) return false;
                r = terms[0] - terms[1];
            }
            else if (!strcmp(ch,"$product")) {
                if (terms.size() != 2) return false;
                r = terms[0] * terms[1];
            }
            else if (!strcmp(ch,"$distinct")) {
                if (terms.size() != 2) return false;
                r = terms[0] != terms[1];
            }
            else if (!strcmp(ch,"$to_int")) {
                if (terms.size() != 1) return false;
                r = z3::expr(r.ctx(), Z3_mk_int2real(r.ctx(), terms[0]));
            }
            else if (!strcmp(ch,"$to_real")) {
                if (terms.size() != 1) return false;
                r = z3::expr(r.ctx(), Z3_mk_real2int(r.ctx(), terms[0]));
            }
            else if (m_decls.find(fn, fun)) {
                r = fun(terms);
            }
            else if (true) {
                z3::func_decl func(m_context);
                func = m_context.function(fn, sorts, s);
                r = func(terms);
            }
            else {
                return mk_error(f->child(0), "atomic, defined or system word");
            }                
            return true;
        }
        return mk_error(f, "function");
    }

    bool check_app(z3::func_decl& f, unsigned num, z3::expr const* args) {
        if (f.arity() == num) {
            for (unsigned i = 0; i < num; ++i) {
                if (!eq(args[i].get_sort(), f.domain(i))) {
                    return false;
                }
            }
            return true;
        }
        else {
            return true;
        }
    }

    bool mk_args(TreeNode* args, z3::expr_vector& result) {
        z3::expr t(m_context);
        while (args) {
            if (!term(args->child(0), m_univ, t)) return false;
            result.push_back(t);
            args = args->child(2);
        }
        return true;
    }


    bool find_bound(char const* v, z3::expr& b) {
        for (unsigned l = m_bound.size(); l > 0; ) {
            --l;
            if (v == m_bound[l].decl().name().str()) {
                b = m_bound[l];
                return true;
            }
        }
        return false;
    }

    bool mk_id(TreeNode* f, char const*& sym) {
        char const* name = f->symbol();
        if (!strcmp(name, "tff_untyped_atom") ||
            !strcmp(name, "functor") ||
            !strcmp(name, "system_functor")) {
            return mk_id(f->child(0), sym);
        }
        if (!strcmp(name, "atomic_word") ||
            !strcmp(name, "atomic_system_word")) {
            sym = f->child(0)->symbol();
            return true;
        }
        return mk_error(f, "atom");
    }

    bool mk_let(TreeNode* let_vars, TreeNode* f, z3::expr& fml) {

        return mk_error(f, "let construct is not handled");
    }

    FILE* open_file(char const* filename) {
        FILE* fp = 0;
#ifdef _WINDOWS
        if (0 > fopen_s(&fp, filename, "r") || fp == 0) {
            fp = 0;
        }
#else
        fp = fopen(filename, "r");
#endif
        return fp;
    }

    bool is_sep(char s) {
        return s == '/' || s == '\\';
    }

    void add_separator(const char* rel_name, std::string& inc_name) {
        size_t sz = inc_name.size();
        if (sz == 0) return;
        if (sz > 0 && is_sep(inc_name[sz-1])) return;
        if (is_sep(rel_name[0])) return;
        inc_name += "/";
    }

    void append_rel_name(const char * rel_name, std::string& inc_name) {
        if (rel_name[0] == '\'') {
            add_separator(rel_name+1, inc_name);
            inc_name.append(rel_name+1);
            inc_name.resize(inc_name.size()-1);
        }
        else {
            add_separator(rel_name, inc_name);
            inc_name.append(rel_name);
        }
    }

    bool mk_filename(const char *rel_name, unsigned num_sep, std::string& inc_name) {
        unsigned sep1 = 0, sep2 = 0, sep3 = 0;
        size_t len = strlen(m_filename);
        for (unsigned i = 0; i < len; ++i) {
            if (is_sep(m_filename[i])) {
                sep3 = sep2;
                sep2 = sep1;
                sep1 = i;
            }
        }
        if ((num_sep == 3) && sep3 > 0) {
            inc_name.append(m_filename,sep3+1);
        }
        if ((num_sep == 2) && sep2 > 0) {
            inc_name.append(m_filename,sep2+1);
        }
        if ((num_sep == 1) && sep1 > 0) {
            inc_name.append(m_filename,sep1+1);
        }
        append_rel_name(rel_name, inc_name);
        return file_exists(inc_name.c_str());        
    }

    bool file_exists(char const* filename) {
        FILE* fp = open_file(filename);
        if (!fp) {
            return false;
        }
        fclose(fp);
        return true;
    }

    bool mk_env_filename(const char* rel_name, std::string& inc_name) {
#ifdef _WINDOWS
        char buffer[1024];
        size_t sz;
        errno_t err = getenv_s( 
            &sz,
            buffer,
            "$TPTP");
        if (err != 0) {
            return false;
        }        
#else
        char const* buffer = getenv("$TPTP");
        if (!buffer) {
            return false;
        }
#endif
        inc_name = buffer;
        append_rel_name(rel_name, inc_name);
        return file_exists(inc_name.c_str());
    }

    void get_cnf_variables(TreeNode* t, symbol_set& symbols) {
        std::vector<TreeNode*> todo;
        todo.push_back(t);
        while (!todo.empty()) {
            t = todo.back();
            todo.pop_back();
            if (!t) continue;
            if (!strcmp(t->symbol(),"variable")) {
                z3::symbol sym = symbol(t->child(0)->symbol());
                symbols.insert(sym);
            }
            else {
                for (unsigned i = 0; i < 10; ++i) {
                    todo.push_back(t->child(i));
                }                
            }            
        }
    }

    z3::symbol symbol(char const* s) {
        return m_context.str_symbol(s);
    }

    z3::sort mk_sort(char const* s) {
        return mk_sort(symbol(s));
    }

    z3::sort mk_sort(z3::symbol& s) {
        return z3::sort(m_context, Z3_mk_uninterpreted_sort(m_context, s));
    }
    
public:
    env(z3::context& ctx):
        m_context(ctx),
        m_bound(ctx),
        m_univ(mk_sort("$i")),
        m_filename(0) {
        m_nodes = 0;
        m_region = new region();
        m_defined_sorts.insert(symbol("$i"),    m_univ);
        m_defined_sorts.insert(symbol("$o"),    m_context.bool_sort());
        m_defined_sorts.insert(symbol("$real"), m_context.real_sort());
        m_defined_sorts.insert(symbol("$int"),  m_context.int_sort());

    }

    ~env() {
        delete m_region;
        m_region = 0;        
    }
    bool parse(const char* filename, named_formulas& fmls);
    static void register_node(TreeNode* t) { m_nodes->push_back(t); }
    static region& r() { return *m_region; }
};    

std::vector<TreeNode*>* env::m_nodes = 0;
region* env::m_region = 0;

#  define P_USERPROC
#  define P_ACT(ss) if(verbose)printf("%7d %s\n",yylineno,ss);
#  define P_BUILD(sym,A,B,C,D,E,F,G,H,I,J) new (env::r()) TreeNode(env::r(), sym,A,B,C,D,E,F,G,H,I,J)
#  define P_TOKEN(tok,symbolIndex) MkToken(env::r(), tok,symbolIndex)
#  define P_PRINT(ss) env::register_node(ss) 


// ------------------------------------------------------
// created by YACC.
#include "tptp5.tab.c"

extern FILE* yyin;

    
bool env::parse(const char* filename, named_formulas& fmls) {
    std::vector<TreeNode*> nodes;
    flet<char const*> fn(m_filename, filename);
    flet<std::vector<TreeNode*>*> fnds(m_nodes, &nodes);

    FILE* fp = open_file(filename);
    if (!fp) {
        std::cout << "Could not open file " << filename << "\n";
        return false;
    }
    yyin = fp;
    int result = yyparse();
    fclose(fp);

    for (unsigned i = 0; i < nodes.size(); ++i) {
        TreeNode* cl = nodes[i];
        if (cl && !mk_input(cl, fmls)) {
            return false;
        }
    }

    return 0 == result;
}

class pp_tptp {
    z3::context& ctx;
    std::vector<z3::symbol>  names;
    std::vector<z3::sort>    sorts;
    std::vector<z3::func_decl> funs;
    std::vector<z3::expr>    todo;
    std::set<unsigned>       seen_ids;
public:
    pp_tptp(z3::context& ctx): ctx(ctx) {}


    void display_func_decl(std::ostream& out, z3::func_decl& f) {
        out << "tff(" << f.name() << "_type, type, (\n   " << f.name() << ": ";
        unsigned na = f.arity();
        switch(na) {
        case 0:
            break;
        case 1:
            display_sort(out, f.domain(0));
            out << " > ";
            break;
        default:
            out << "( ";
            for (unsigned j = 0; j < na; ++j) {
                display_sort(out, f.domain(j));
                if (j + 1 < na) {
                    out << " * ";
                }
            }
            out << " ) > ";
        }
        display_sort(out, f.range());
        out << ")).\n";
    }

    void display_axiom(std::ostream& out, z3::expr& e) {
        out << "tff(formula, axiom,\n";
        display(out, e);
        out << ").\n";
    }

    void display(std::ostream& out, z3::expr& e) {
        if (e.is_numeral()) {
            out << e;
        }
        else if (e.is_var()) {
            unsigned idx = Z3_get_index_value(ctx, e);
            out << names[names.size()-1-idx];
        }
        else if (e.is_app()) {
            switch(e.decl().decl_kind()) {
            case Z3_OP_TRUE:
                out << "$true";
                break;
            case Z3_OP_FALSE:
                out << "$false";
                break;
            case Z3_OP_AND:
                display_infix(out, "&", e);
                break;
            case Z3_OP_OR:
                display_infix(out, "|", e);
                break;
            case Z3_OP_IMPLIES:
                display_infix(out, "=>", e);
                break;
            case Z3_OP_NOT:
                out << "(~";
                display(out, e.arg(0));
                out << ")";
                break;
            case Z3_OP_EQ:
                display_infix(out, "=", e);
                break;
            case Z3_OP_IFF:
                display_infix(out, "<=>", e);
                break;
            case Z3_OP_XOR:
                display_infix(out, "<~>", e);
                break;
            case Z3_OP_MUL:  
                display_prefix(out, "$product", e); // TBD binary
                break;               
            case Z3_OP_ADD:
                display_prefix(out, "$sum", e); // TBD binary 
                break;     
            case Z3_OP_SUB:
                display_prefix(out, "$difference", e); 
                break;     
            case Z3_OP_LE:
                display_prefix(out, "$lesseq", e);
                break;     
            case Z3_OP_GE:
                display_prefix(out, "$greatereq", e);
                break;     
            case Z3_OP_LT:
                display_prefix(out, "$less", e);
                break;     
            case Z3_OP_GT:
                display_prefix(out, "$greater", e);
                break;     
            case Z3_OP_UMINUS:
                display_prefix(out, "$uminus", e);
                break;            
            case Z3_OP_DIV:
                display_prefix(out, "$quotient", e);
                break;
            case Z3_OP_IS_INT:
                display_prefix(out, "$is_int", e);
                break;
            case Z3_OP_TO_REAL:
                display_prefix(out, "$to_real", e);
                break;
            case Z3_OP_TO_INT:
                display_prefix(out, "$to_int", e);
                break;
            case Z3_OP_IDIV:
                display_prefix(out, "$quotient_e", e);
                break;
            case Z3_OP_MOD:
                display_prefix(out, "$remainder_e", e);
                break;                
            case Z3_OP_ITE:
            case Z3_OP_DISTINCT:
            case Z3_OP_REM:
                // TBD
                display_app(out, e);
                break;
            default:
                display_app(out, e);
                break;
            }
        }
        else if (e.is_quantifier()) {
            bool is_forall = Z3_is_quantifier_forall(ctx, e);
            unsigned nb = Z3_get_quantifier_num_bound(ctx, e);

            out << (is_forall?"!":"?") << "[";
            for (unsigned i = 0; i < nb; ++i) {
                Z3_symbol n = Z3_get_quantifier_bound_name(ctx, e, i);
                z3::symbol s(ctx, n);
                names.push_back(s);
                z3::sort srt(ctx, Z3_get_quantifier_bound_sort(ctx, e, i));
                out << s << ": ";
                display_sort(out, srt);
                if (i + 1 < nb) {
                    out << ", ";
                }
            }
            out << "] : ";
            display(out, e.body());
            for (unsigned i = 0; i < nb; ++i) {
                names.pop_back();
            }
        }
    }

    void display_app(std::ostream& out, z3::expr& e) {
        if (e.is_const()) {
            out << e;
            return;
        }
        out << e.decl().name() << "(";
        unsigned n = e.num_args();
        for(unsigned i = 0; i < n; ++i) {
            display(out, e.arg(i));
            if (i + 1 < n) {
                out << ", ";
            }
        }
        out << ")";
    }

    void display_sort(std::ostream& out, z3::sort& s) {
        if (s.is_int()) {
            out << "$int";
        }
        else if (s.is_real()) {
            out << "$real";
        }
        else if (s.is_bool()) {
            out << "$o";
        }
        else {
            out << s;
        }
    }

    void display_infix(std::ostream& out, char const* conn, z3::expr& e) {
        out << "(";
        unsigned sz = e.num_args();
        for (unsigned i = 0; i < sz; ++i) {
            display(out, e.arg(i));
            if (i + 1 < sz) {
                out << " " << conn << " ";
            }
        }
        out << ")";
    }

    void display_prefix(std::ostream& out, char const* conn, z3::expr& e) {
        out << conn << "(";
        unsigned sz = e.num_args();
        for (unsigned i = 0; i < sz; ++i) {
            display(out, e.arg(i));
            if (i + 1 < sz) {
                out << ", ";
            }
        }
        out << ")";
    }

    void display_sort_decls(std::ostream& out) {
        for (unsigned i = 0; i < sorts.size(); ++i) {
            display_sort_decl(out, sorts[i]);
        }
    }
    
    void display_sort_decl(std::ostream& out, z3::sort& s) {
        out << "tff(" << s << "_type, type, (" << s << ": $tType)).\n";
    }


    void display_func_decls(std::ostream& out) {
        for (unsigned i = 0; i < funs.size(); ++i) {
            display_func_decl(out, funs[i]);
        }
    }

    bool contains_id(unsigned id) const {
        return seen_ids.find(id) != seen_ids.end();
    }

    void collect_decls(z3::expr& e) {
        todo.push_back(e);
        while (!todo.empty()) {
            z3::expr e = todo.back();
            todo.pop_back();
            unsigned id = Z3_get_ast_id(ctx, e);
            if (contains_id(id)) {
                continue;
            }
            seen_ids.insert(id);
            if (e.is_app()) {
                collect_fun(e.decl());
                unsigned sz = e.num_args();
                for (unsigned i = 0; i < sz; ++i) {
                    todo.push_back(e.arg(i));
                }
            }
            else if (e.is_quantifier()) {
                todo.push_back(e.body());
            }
            else if (e.is_var()) {
                collect_sort(e.get_sort());
            }
        }
    }

    void collect_sort(z3::sort& s) {
        unsigned id = Z3_get_sort_id(ctx, s);
        if (s.sort_kind() == Z3_UNINTERPRETED_SORT && 
            contains_id(id)) {
            seen_ids.insert(id);
            sorts.push_back(s);
        }
    }

    void collect_fun(z3::func_decl& f) {
        unsigned id = Z3_get_func_decl_id(ctx, f);
        if (contains_id(id)) {
            return;
        }
        seen_ids.insert(id);
        if (f.decl_kind() == Z3_OP_UNINTERPRETED) {
            funs.push_back(f);
        }
        for (unsigned i = 0; i < f.arity(); ++i) {
            collect_sort(f.domain(i));
        }
        collect_sort(f.range());
    }    
};

static char* g_input_file = 0;
static bool g_display_smt2 = false;
static bool g_generate_model = false;
static bool g_generate_proof = false;
static bool g_generate_core = false;
static bool g_display_statistics = false;
static bool g_first_interrupt = true;
static bool g_smt2status = false;
static double g_start_time = 0;
static z3::solver*   g_solver = 0;
static z3::context*  g_context = 0;
static std::ostream* g_out = &std::cout;



static void display_usage() {
    unsigned major, minor, build_number, revision_number;
    Z3_get_version(&major, &minor, &build_number, &revision_number);
    std::cout << "Z3tptp [" << major << "." << minor << "." << build_number << "." << revision_number << "] (c) 2006-20**. Microsoft Corp.\n";
    std::cout << "Usage: tptp [options] [-file]file\n";
    std::cout << "  -h, -?       prints this message.\n";
    std::cout << "  -smt2        print SMT-LIB2 benchmark.\n";
    std::cout << "  -m, -model   generate model.\n";
    std::cout << "  -p, -proof   generate proof.\n";
    std::cout << "  -c, -core    generate unsat core of named formulas.\n";
    std::cout << "  -st, -statistics display statistics.\n";
    std::cout << "  -smt2status  display status in smt2 format instead of SZS.\n";
    // std::cout << "  -v, -verbose  verbose mode.\n";
    std::cout << "  -o:<output-file> file to place output in.\n";
}


static void display_statistics() {
    if (g_solver && g_display_statistics) {
        std::cout.flush();
        std::cerr.flush();
        double end_time = static_cast<double>(clock());
        z3::stats stats = g_solver->statistics();
        std::cout << stats << "\n";
        std::cout << "time:   " << (end_time - g_start_time)/CLOCKS_PER_SEC << " secs\n";
    }
}

static void on_ctrl_c(int) {
    if (g_context && g_first_interrupt) {
        Z3_interrupt(*g_context);
        g_first_interrupt = false;
    }
    else {
        signal (SIGINT, SIG_DFL);
        display_statistics();
        raise(SIGINT);
    }
}


void parse_cmd_line_args(int argc, char ** argv) {
    g_input_file = 0;
    g_display_smt2 = false;
    int i = 1;
    while (i < argc) {
        char* arg = argv[i];
        if (arg[0] == '-' || arg[0] == '/') {
            ++arg;
            while (*arg == '-') {
                ++arg;
            }
            char * opt_arg = 0;
            char * colon = strchr(arg, ':');
            if (colon) {
                opt_arg = colon + 1;
                *colon = 0;
            }
            if (!strcmp(arg,"h") || !strcmp(arg,"help") || !strcmp(arg,"?")) {
                display_usage();
                exit(0);
            }
            if (!strcmp(arg,"p") || !strcmp(arg,"proof")) {
                g_generate_proof = true;
            }
            else if (!strcmp(arg,"m") || !strcmp(arg,"model")) {
                g_generate_model = true;
            }
            else if (!strcmp(arg,"c") || !strcmp(arg,"core")) {
                g_generate_core = true;
            }
            else if (!strcmp(arg,"st") || !strcmp(arg,"statistics")) {
                g_display_statistics = true;
            }
            else if (!strcmp(arg,"smt2status")) {
                g_smt2status = true;
            }
            else if (!strcmp(arg,"o")) {
                if (opt_arg) {
                    g_out = new std::ofstream(opt_arg);
                    if (g_out->bad() || g_out->fail()) {
                        std::cout << "Could not open file of output: " << opt_arg << "\n";
                        exit(0);
                    }
                }
                else {
                    display_usage();
                    exit(0);
                }
            }
            else if (!strcmp(arg,"smt2")) {
                g_display_smt2 = true;

            }
            else if (!strcmp(arg, "file")) {
                g_input_file = opt_arg;
            }
        }
        else {
            g_input_file = arg;
        }
        ++i;
    }

    if (!g_input_file) {
        display_usage();
        exit(0);
    }
}

static bool is_smt2_file(char const* filename) {
    size_t len = strlen(filename);
    return (len > 4 && !strcmp(filename + len - 5,".smt2"));    
}

static void check_error(z3::context& ctx) {
    Z3_error_code e = Z3_get_error_code(ctx);
    if (e != Z3_OK) {
        std::cout << Z3_get_error_msg_ex(ctx, e) << "\n";
        exit(1);
    }
}

static void display_tptp(std::ostream& out) {
    // run SMT2 parser, pretty print TFA format.
    z3::context ctx;
    Z3_ast _fml = Z3_parse_smtlib2_file(ctx, g_input_file, 0, 0, 0, 0, 0, 0);
    check_error(ctx);
    z3::expr fml(ctx, _fml);

    pp_tptp pp(ctx);

    pp.collect_decls(fml);

    pp.display_sort_decls(out);

    pp.display_func_decls(out);

    if (fml.decl().decl_kind() == Z3_OP_AND) {
        for (unsigned i = 0; i < fml.num_args(); ++i) {
            pp.display_axiom(out, fml.arg(i));
        }
    }
    else {
        pp.display_axiom(out, fml);
    }
}

static void print_model(z3::context& ctx, z3::model& model) {
    std::cout << model << "\n";
    return;
// TODO:
    unsigned nc = model.num_consts();
    unsigned nf = model.num_funcs();
    for (unsigned i = 0; i < nc; ++i) {
        z3::func_decl f = model.get_const_decl(i);
        z3::expr e = model.get_const_interp(f);
    }

    for (unsigned i = 0; i < nf; ++i) {
        z3::func_decl f = model.get_func_decl(i);
        z3::func_interp fi = model.get_func_interp(f);
        std::cout << f << "\n";
    }
}

static void display_smt2(std::ostream& out) {
    z3::config config;
    z3::context ctx(config);
    named_formulas fmls;
    env env(ctx);
    if (!env.parse(g_input_file, fmls)) {
        return;
    }

    unsigned num_assumptions = fmls.m_formulas.size();

    Z3_ast* assumptions = new Z3_ast[num_assumptions];
    for (unsigned i = 0; i < num_assumptions; ++i) {
        assumptions[i] = fmls.m_formulas[i].first;
    }
    Z3_string s = Z3_benchmark_to_smtlib_string(ctx, "yes", "logic", "unknown", "", 
                                                num_assumptions, assumptions, ctx.bool_val(true));
    out << s << "\n";
    delete[] assumptions;
}

static void prove_tptp() {
    z3::config config;
    if (g_generate_proof) {
        config.set("proof", true);
    }    
    z3::context ctx(config);
    z3::solver solver(ctx);
    g_solver  = &solver;
    g_context = &ctx;

    named_formulas fmls;
    env env(ctx);
    if (!env.parse(g_input_file, fmls)) {
        std::cout << "SZS status GaveUp\n";
        return;
    }

    unsigned num_assumptions = fmls.m_formulas.size();

    z3::check_result result;

    if (g_generate_core) {
        z3::expr_vector assumptions(ctx);
        
        for (unsigned i = 0; i < num_assumptions; ++i) {
            z3::expr pred = ctx.constant(fmls.m_formulas[i].second, ctx.bool_sort());
            z3::expr def = fmls.m_formulas[i].first == pred;
            solver.add(def);
            assumptions.push_back(pred);
        }
        result = solver.check(assumptions);
    }
    else {
        for (unsigned i = 0; i < num_assumptions; ++i) {
            solver.add(fmls.m_formulas[i].first);
        }        
        result = solver.check();
    }

    switch(result) {
    case z3::unsat:
        if (g_smt2status) {
            std::cout << result << "\n";
        }
        else if (fmls.has_conjecture()) {
            std::cout << "SZS status Theorem\n";
        }
        else {
            std::cout << "SZS status Unsatisfiable\n";
        }
        if (g_generate_proof) {
            // TBD:
            std::cout << solver.proof() << "\n";
        }
        if (g_generate_core) {
            z3::expr_vector core = solver.unsat_core();
            std::cout << "SZS core ";
            for (unsigned i = 0; i < core.size(); ++i) {
                std::cout << core[i] << " ";
            }
            std::cout << "\n";
        }
        break;
    case z3::sat:
        if (g_smt2status) {
            std::cout << result << "\n";
        }
        else if (fmls.has_conjecture()) {
            std::cout << "SZS status CounterSatisfiable\n";            
        }
        else {
            std::cout << "SZS status Satisfiable\n";
        }
        if (g_generate_model) {
            print_model(ctx, solver.get_model());
        }
        break;
    case z3::unknown:
        if (g_smt2status) {
            std::cout << result << "\n";
        }
        else if (!g_first_interrupt) {
            std::cout << "SZS status Interrupted\n";
        }
        else {
            std::cout << "SZS status GaveUp\n";
            std::string reason = solver.reason_unknown();
            std::cout << "SZS reason " << reason << "\n";
        }
        break;
    }    
    display_statistics();
}

void main(int argc, char** argv) {

    std::ostream* out = &std::cout;
    g_start_time = static_cast<double>(clock());
    signal(SIGINT, on_ctrl_c);

    parse_cmd_line_args(argc, argv);
    
    if (is_smt2_file(g_input_file)) {
        display_tptp(*g_out);
    }
    else if (g_display_smt2) {
        display_smt2(*g_out);
    }
    else {
        prove_tptp();
    }
}


/**
TODOs:
   - model printing
   - proof printing
   - port Z3 parameters into engine
   - ite
   - verbose mode
   - timeout
 */
