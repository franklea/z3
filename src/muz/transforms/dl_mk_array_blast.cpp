/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    dl_mk_array_blast.cpp

Abstract:

    Remove array stores from rules.

Author:

    Nikolaj Bjorner (nbjorner) 2012-11-23

Revision History:

--*/

#include "dl_mk_array_blast.h"
#include "qe_util.h"

namespace datalog {


    mk_array_blast::mk_array_blast(context & ctx, unsigned priority) : 
        rule_transformer::plugin(priority, false),
        m_ctx(ctx),
        m(ctx.get_manager()), 
        a(m),
        rm(ctx.get_rule_manager()),
        m_rewriter(m, m_params),
        m_simplifier(ctx),
        m_sub(m),
        m_next_var(0) {
        m_params.set_bool("expand_select_store",true);
        m_rewriter.updt_params(m_params);
    }

    mk_array_blast::~mk_array_blast() {
    }

    bool mk_array_blast::is_store_def(expr* e, expr*& x, expr*& y) {
        if (m.is_iff(e, x, y) || m.is_eq(e, x, y)) {
            if (!a.is_store(y)) {
                std::swap(x,y);
            }
            if (is_var(x) && a.is_store(y)) {
                return true;
            }
        }
        return false;
    }

    expr* mk_array_blast::get_select(expr* e) const {
        while (a.is_select(e)) {
            e = to_app(e)->get_arg(0);
        }
        return e;
    }

    void mk_array_blast::get_select_args(expr* e, ptr_vector<expr>& args) const {
        while (a.is_select(e)) {
            app* ap = to_app(e);
            for (unsigned i = 1; i < ap->get_num_args(); ++i) {
                args.push_back(ap->get_arg(i));
            }
            e = ap->get_arg(0);
        }
    }
     
    bool mk_array_blast::insert_def(rule const& r, app* e, var* v) {
        //
        // For the Ackermann reduction we would like the arrays 
        // to be variables, so that variables can be 
        // assumed to represent difference (alias) 
        // classes. Ehm., Soundness of this approach depends on 
        // if the arrays are finite domains...
        // 

        if (!is_var(get_select(e))) {
            return false;
        }
        if (v) {
            m_sub.insert(e, v);
            m_defs.insert(e, to_var(v));
        }
        else {
            if (m_next_var == 0) {
                ptr_vector<sort> vars;
                r.get_vars(vars);
                m_next_var = vars.size() + 1;
            }
            v = m.mk_var(m_next_var, m.get_sort(e));
            m_sub.insert(e, v);
            m_defs.insert(e, v);
            ++m_next_var;
        }
        return true;
    }
    
    bool mk_array_blast::ackermanize(rule const& r, expr_ref& body, expr_ref& head) {
        expr_ref_vector conjs(m);
        qe::flatten_and(body, conjs);
        m_defs.reset();
        m_sub.reset();
        m_next_var = 0;
        ptr_vector<expr> todo;
        todo.push_back(head);
        for (unsigned i = 0; i < conjs.size(); ++i) {
            expr* e = conjs[i].get();
            expr* x, *y;
            if (m.is_eq(e, x, y) || m.is_iff(e, x, y)) {
                if (a.is_select(y)) {
                    std::swap(x,y);
                }
                if (a.is_select(x) && is_var(y)) {
                    if (!insert_def(r, to_app(x), to_var(y))) {
                        return false;
                    }
                }
            }
            if (a.is_select(e) && !insert_def(r, to_app(e), 0)) {
                return false;
            }
            todo.push_back(e);
        }
        // now make sure to cover all occurrences.
        ast_mark mark;
        while (!todo.empty()) {
            expr* e = todo.back();
            todo.pop_back();
            if (mark.is_marked(e)) {
                continue;
            }
            mark.mark(e, true);
            if (is_var(e)) {
                continue;
            }
            if (!is_app(e)) {
                return false;
            }
            app* ap = to_app(e);
            if (a.is_select(ap) && !m_defs.contains(ap)) {
                if (!insert_def(r, ap, 0)) {
                    return false;
                }
            }
            if (a.is_select(e)) {
                get_select_args(e, todo);
                continue;
            }
            for (unsigned i = 0; i < ap->get_num_args(); ++i) {
                todo.push_back(ap->get_arg(i));
            }
        }
        m_sub(body);
        m_sub(head);
        conjs.reset();

        // perform the Ackermann reduction by creating implications
        // i1 = i2 => val1 = val2 for each equality pair:
        // (= val1 (select a_i i1))
        // (= val2 (select a_i i2))
        defs_t::iterator it1 = m_defs.begin(), end = m_defs.end();
        for (; it1 != end; ++it1) {
            app* a1 = it1->m_key;
            var* v1 = it1->m_value;
            defs_t::iterator it2 = it1;
            ++it2;
            for (; it2 != end; ++it2) {
                app* a2 = it2->m_key;
                var* v2 = it2->m_value;
                if (get_select(a1) != get_select(a2)) {
                    continue;
                }
                expr_ref_vector eqs(m);
                ptr_vector<expr> args1, args2;
                get_select_args(a1, args1);
                get_select_args(a2, args2);
                for (unsigned j = 0; j < args1.size(); ++j) {
                    eqs.push_back(m.mk_eq(args1[j], args2[j]));
                }
                conjs.push_back(m.mk_implies(m.mk_and(eqs.size(), eqs.c_ptr()), m.mk_eq(v1, v2)));
            }
        }
        if (!conjs.empty()) {
            conjs.push_back(body);
            body = m.mk_and(conjs.size(), conjs.c_ptr());
        }
        m_rewriter(body);   
        return true;
    }

    bool mk_array_blast::blast(rule& r, rule_set& rules) {
        unsigned utsz = r.get_uninterpreted_tail_size();
        unsigned tsz = r.get_tail_size();
        expr_ref_vector conjs(m), new_conjs(m);
        expr_ref tmp(m);
        expr_safe_replace sub(m);
        bool change = false;
        bool inserted = false;

        for (unsigned i = 0; i < utsz; ++i) {
            new_conjs.push_back(r.get_tail(i));
        }
        for (unsigned i = utsz; i < tsz; ++i) {
            conjs.push_back(r.get_tail(i));
        }
        qe::flatten_and(conjs);
        for (unsigned i = 0; i < conjs.size(); ++i) {
            expr* x, *y, *e = conjs[i].get();
            
            if (is_store_def(e, x, y)) {
                // enforce topological order consistency:
                uint_set lhs = rm.collect_vars(x);
                uint_set rhs_vars = rm.collect_vars(y);
                lhs &= rhs_vars;
                if (!lhs.empty()) {
                    TRACE("dl", tout << "unusable equality " << mk_pp(e, m) << "\n";);
                    new_conjs.push_back(e);
                }
                else {
                    sub.insert(x, y);
                    inserted = true;
                }
            }
            else {
                m_rewriter(e, tmp);
                change = change || (tmp != e);
                new_conjs.push_back(tmp);
            }
        }
        
        expr_ref fml2(m), body(m), head(m);
        body = m.mk_and(new_conjs.size(), new_conjs.c_ptr());
        head = r.get_head();
        sub(body);
        m_rewriter(body);
        sub(head);
        m_rewriter(head);
        change = ackermanize(r, body, head) || change;
        if (!inserted && !change) {
            rules.add_rule(&r);
            return false;
        }

        fml2 = m.mk_implies(body, head);
        proof_ref p(m);
        rule_set new_rules(m_ctx);
        rm.mk_rule(fml2, p, new_rules, r.name());

        rule_ref new_rule(rm);
        if (m_simplifier.transform_rule(new_rules.last(), new_rule)) {
            rules.add_rule(new_rule.get());
            rm.mk_rule_rewrite_proof(r, *new_rule.get());
            TRACE("dl", new_rule->display(m_ctx, tout << "new rule\n"););
        }
        return true;
    }
    
    rule_set * mk_array_blast::operator()(rule_set const & source) {

        rule_set* rules = alloc(rule_set, m_ctx);
        rules->inherit_predicates(source);
        rule_set::iterator it = source.begin(), end = source.end();
        bool change = false;
        for (; !m_ctx.canceled() && it != end; ++it) {
            change = blast(**it, *rules) || change;
        }
        if (!change) {
            dealloc(rules);
            rules = 0;
        }        
        return rules;        
    }

};


