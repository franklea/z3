/*++
Copyright (c) 2013 Microsoft Corporation

Module Name:

    eval_check.cpp

Abstract:

    <abstract>

Author:

    Andy Reynolds 2013-04-21.

--*/

#include"eval_check.h"
#include"ast_pp.h"
#include"model_construct.h"
#include"model_check.h"

//#define EVAL_CHECK_DEBUG
#define USE_BINARY_SEARCH

using namespace qsolver;

struct lt_index { 
public:
    static int compare(annot_entry * tc1, annot_entry * tc2) {
        SASSERT(tc1->get_size()==tc2->get_size());
        for (unsigned i=0; i<tc1->get_size(); i++) {
            if (tc1->get_value(i)<tc2->get_value(i)) {
                return 1;
            }
            else if (tc1->get_value(i)>tc2->get_value(i)) {
                return -1;
            }
        }
        return 0;
    }
    static int compare(expr_ref_buffer & vals, annot_entry * tc2) {
        SASSERT(vals.size()==tc2->get_size());
        for (unsigned i=0; i<vals.size(); i++) {
            if (vals[i]<tc2->get_value(i)) {
                return 1;
            }
            else if (vals[i]>tc2->get_value(i)) {
                return -1;
            }
        }
        return 0;
    }
public:
    simple_def & m_sdf;
    lt_index(simple_def & sdf) : m_sdf(sdf) {}
    bool operator()(annot_entry * tc1, annot_entry * tc2) const {
        return compare(tc1, tc2) == -1;
    }
};


annot_entry * annot_entry::mk(mc_context & mc, unsigned arity) {
    //small_object_allocator & allocator = _m.get_allocator();
    void * mem  = mc.allocate(sizeof(expr) + arity * 2 * sizeof(expr*) );
    return new (mem) annot_entry(arity);
}


bool annot_entry::is_value() {
    for (unsigned i=0; i<get_size(); i++) {
        if (!m_vec[i]) {
            return false;
        }
    }
    return true;
}

bool annot_entry_trie::add(mc_context & mc, annot_entry * c, unsigned index, unsigned data_val) {
    if (index==c->get_size()) {
        if (m_data==0) {
            m_data = data_val+1;
            return true;
        }
        else {
            return false;
        }
    }
    else {
        expr * curr = c->get_value(index);
        annot_entry_trie * ct;
        if (m_children.find(curr, ct)) {
            return ct->add(mc, c, index+1, data_val);
        }
        else {
            void * mem = mc.allocate(sizeof(annot_entry_trie));
            ct = new (mem) annot_entry_trie;
            m_children.insert(curr, ct);
            return ct->add(mc, c, index+1, data_val);
        }
    }
}

bool annot_entry_trie::evaluate(mc_context & mc, annot_entry * c, unsigned index, unsigned & data_val) {
    if (index==c->get_size()) {
        if (m_data==0) {
            return false;
        }
        else {
            data_val = m_data-1;
            return true;
        }
    }
    else {
        annot_entry_trie * ct;
        if (m_children.find(c->get_value(index), ct)) {
            return ct->evaluate(mc, c, index+1, data_val);
        }
        else {
            return false;
        }
    }
}

bool annot_entry_trie::evaluate(mc_context & mc, expr_ref_buffer & vals, unsigned index, unsigned & data_val) {
    if (index==vals.size()) {
        if (m_data==0) {
            return false;
        }
        else {
            data_val = m_data-1;
            return true;
        }
    }
    else {
        annot_entry_trie * ct;
        if (m_children.find(vals[index], ct)) {
            return ct->evaluate(mc, vals, index+1, data_val);
        }
        else {
            return false;
        }
    }
}

#ifdef USE_BINARY_SEARCH

expr * simple_def::evaluate(mc_context & mc, annot_entry * c, bool ignore_else) {
    unsigned sz = m_conds.size();
    if (sz == 0) {
        return ignore_else ? 0 : m_else;
    }
    if (!m_sorted) {
        //sort the definition
        lt_index lti(*this);
        std::sort(m_conds.begin(), m_conds.end(), lti);
        for (unsigned i=0; i<(sz-1); i++) { 
            SASSERT(lt_index::compare(m_conds[i],m_conds[i+1])==-1);
        }
        m_sorted = true;
    }
    int low  = 0;
    int high = sz - 1;
    while (true) {
        int mid            = low + ((high - low)/2);
        int s = lt_index::compare(c, m_conds[mid]);
        if (s > 0) {
            low = mid + 1;
        }
        else if (s < 0) {
            high = mid - 1;
        }
        else {
            return m_conds[mid]->get_result();
        }
        if (low > high) {
            return ignore_else ? 0 : m_else;
        }
    }
    SASSERT(false);
    return ignore_else ? 0 : m_else;
}

expr * simple_def::evaluate(mc_context & mc, expr_ref_buffer & vals, bool ignore_else) {
    unsigned sz = m_conds.size();
    if (sz == 0) {
        return ignore_else ? 0 : m_else;
    }
    if (!m_sorted) {
        //sort the definition
        lt_index lti(*this);
        std::sort(m_conds.begin(), m_conds.end(), lti);
        for (unsigned i=0; i<(sz-1); i++) { 
            SASSERT(lt_index::compare(m_conds[i],m_conds[i+1])==-1);
        }
        m_sorted = true;
    }
    int low  = 0;
    int high = sz - 1;
    while (true) {
        int mid            = low + ((high - low)/2);
        int s = lt_index::compare(vals, m_conds[mid]);
        if (s > 0) {
            low = mid + 1;
        }
        else if (s < 0) {
            high = mid - 1;
        }
        else {
            return m_conds[mid]->get_result();
        }
        if (low > high) {
            return ignore_else ? 0 : m_else;
        }
    }
    SASSERT(false);
    return ignore_else ? 0 : m_else;
}

bool simple_def::append_entry(mc_context & mc, annot_entry * c) {
    if (!evaluate(mc, c, true)) {
        m_sorted = false;
        m_conds.push_back(c);
        m_unsorted_conds.push_back(c);
        return true;
    }
    return false;
}


#else

expr * simple_def::evaluate(mc_context & mc, annot_entry * c, bool ignore_else) {
    unsigned index;
    if (m_tct.evaluate(mc, c, index)) {
        return m_conds[index]->get_result();
    }
    return ignore_else ? 0 : m_else;
}

expr * simple_def::evaluate(mc_context & mc, expr_ref_buffer & vals, bool ignore_else) {
    unsigned index;
    if (m_tct.evaluate(mc, vals, index)) {
        return m_conds[index]->get_result();
    }
    return ignore_else ? 0 : m_else;
}

bool simple_def::append_entry(mc_context & mc, annot_entry * c) {
    SASSERT(c->is_value());
    if (m_tct.add(mc, c, m_conds.size())) {
        m_conds.push_back(c);
        m_unsorted_conds.push_back(c);
        return true;
    }
    else {
        return false;
    }
}
#endif

void eval_node::notify_evaluation(ptr_vector<eval_node> & active) {
    for (unsigned i=0; i<m_parents.size(); i++) {
        m_parents[i]->m_children_eval_count++;
        TRACE("eval_node", tout << "parent inc " << m_parents[i]->m_children_eval_count << " / " << to_app(m_parents[i]->get_expr())->get_num_args() << "\n";);
        if (m_parents[i]->can_evaluate()) {
            SASSERT(!active.contains(m_parents[i]));
            active.push_back(m_parents[i]);
        }
    }
}
void eval_node::unnotify_evaluation() {
    for (unsigned i=0; i<m_parents.size(); i++) {
        m_parents[i]->m_children_eval_count--;
    }
}

eval_check::eval_check(ast_manager & _m) : m_m(_m) {
    m_eval_check_inst_limited = true;    
    m_eval_check_multiple_patterns = false;
}

eval_node * eval_check::mk_eval_node(mc_context & mc, expr * e, ptr_vector<eval_node> & active, ptr_buffer<eval_node> & vars, 
                                     obj_map< expr, eval_node *> & evals, expr * parent) {
    eval_node * ene;
    if (evals.find(e, ene)) {
        return ene;
    }
    else {
        void * mem = mc.allocate(sizeof(eval_node));
        ene = new (mem) eval_node(e);
        if (!is_ground(e) && is_app(e)) {
            for (unsigned i=0; i<to_app(e)->get_num_args(); i++) {
                expr * ec = to_app(e)->get_arg(i);
                if (mc.is_atomic_value(ec)) {
                    ene->m_children_eval_count++;
                    ene->m_children.push_back(0);
                }
                //else if (is_uninterp(e) && m_cutil.is_var_offset(ec, classify_util::REQ_GROUND)) {
                else if (is_uninterp(e) && is_var(ec)) {
                    ene->m_children_eval_count++;
                    ene->m_vars_to_bind++;
                    ene->m_children.push_back(0);
                }
                else {
                    eval_node * enec = mk_eval_node(mc, ec, active, vars, evals, e);
                    enec->add_parent(ene);
                }
            }
        }
        if (is_ground(e) || ene->can_evaluate()) {
            active.push_back(ene);
        }
        if (is_var(e)) {
            unsigned vid = to_var(e)->get_idx();
            vars[vid] = ene;
        }
        evals.insert(e, ene);
        return ene;
    }
}

lbool eval_check::run(mc_context & mc, model_constructor * mct, quantifier * q, expr_ref_buffer & instantiations, bool & repaired) {
    ptr_vector<eval_node> active;
    ptr_buffer<eval_node> vars;
    vars.resize(q->get_num_decls(),0);
    obj_map< expr, eval_node *> evals;
    mk_eval_node(mc, q->get_expr(), active, vars, evals);

    //std::random_shuffle(active.begin(), active.end());

    TRACE("eval_check", tout << "Run eval check on " << mk_pp(q,m_m) << "\n";
                        tout << "------------------\n";
                        tout << "Evaluation terms are summarized : \n";
                        for (obj_map< expr, eval_node *>::iterator it = evals.begin(); it != evals.end(); ++it) {
                            expr * e = it->m_key;
                            eval_node * en = it->m_value;
                            tout << "   " << mk_pp(e,m_m) << " ";
                            if (active.contains(en)) {
                                tout << "*active*";
                            }
                            tout << "\n";
                        }
                        );
    repaired = false;
    //std::cout << "Eval check " << mk_pp(q,m_m) << "..." << std::endl;

    unsigned prev_size = active.size();
    expr_ref_buffer vsub(m_m);
    expr_ref_buffer esub(m_m);
    for (unsigned i=0; i<q->get_num_decls(); i++) {
        vsub.push_back(0);
        esub.push_back(0);
    }
    m_start_index.reset();
    m_start_score = 0;
    do {
        m_first_time = true;
        if (do_eval_check(mc, mct, q, active, vars, vsub, esub, instantiations, 0, repaired)==l_false) {
            TRACE("eval_check", tout << "Eval check failed on quantifier " << mk_pp(q,m_m) << "\n";);
            return instantiations.empty() ? l_undef : l_false;
        }
        else {
            SASSERT(active.size()==prev_size);
            TRACE("eval_check", tout << "Eval check succeeded on quantifier " << mk_pp(q,m_m) << " " << m_start_index.size() << "\n";);
        }
        //std::cout << "Done." << std::endl;
    } while (m_eval_check_multiple_patterns && instantiations.empty());

    return instantiations.empty() ? l_undef : l_false;
}

lbool eval_check::do_eval_check(mc_context & mc, model_constructor * mct, quantifier * q, ptr_vector<eval_node> & active, ptr_buffer<eval_node> & vars, 
                                expr_ref_buffer & vsub, expr_ref_buffer & esub, 
                                expr_ref_buffer & instantiations, unsigned var_bind_count, bool & repaired) {
    lbool eresult = l_undef;
    unsigned prev_size = active.size();
    bool firstTime = m_first_time;
    if (!active.empty()) {
        unsigned best_index = active.size()-1;
        unsigned max_score = 0;
        for (unsigned i=0; i<active.size(); i++) {
            unsigned ii = (active.size()-1)-i;
            if (active[ii]->can_evaluate() && (!m_first_time || !m_start_index.contains(ii))) {
                unsigned score = 1 + active[ii]->m_vars_to_bind; //TODO : more heuristics
                //get score
                if (score>max_score) {
                    best_index = ii;
                    max_score = score;
                }
            }
        }
        if (max_score==0) {
            return l_false;
        }
        if (m_first_time) {
            if (m_start_index.contains(best_index) || max_score<m_start_score) {
                return l_false;
            }
            else {
                m_start_index.push_back(best_index);
            }
            m_first_time = false;
            m_start_score = max_score;
        }

        eval_node * en = active[best_index];
        active.erase(active.begin()+best_index);
        expr * e = en->get_expr();
        TRACE("eval_check_debug", tout << "Process " << mk_pp(e,m_m) << "\n";);
        expr * result = 0;
        if (is_ground(e)) {
            //just use the evaluator
            result = mc.evaluate(mct, e);
        }
        else {
            //evaluate the expression
            expr_ref_buffer children(m_m);
            sbuffer<unsigned> var_to_bind;
            for (unsigned i=0; i<to_app(e)->get_num_args(); i++) {
                if (en->get_child(i)) {
                    children.push_back(en->get_child(i)->get_evaluation());
                }
                else {
                    expr * ec = to_app(e)->get_arg(i);
                    if (mc.is_atomic_value(ec)) {
                        children.push_back(ec);
                    }
                    else {
                        if (is_uninterp(e) && is_var(ec)) {
                            var * v = to_var(ec);
                            //check if v is already bound
                            unsigned vid = v->get_idx();
                            expr * val_made = 0;
                            if (vsub[vid]) {
                                val_made = vsub[vid];
                            }
                            else {
                                if (!var_to_bind.contains(vid)) {
                                    var_to_bind.push_back(vid);
                                }
                                val_made = v;
                            }
                            children.push_back(val_made);
                        }
                        else {
                            //don't know how to evaluate (start trying relevant domain?)
                            SASSERT(false);
                            return l_false;
                        }
                    }
                }
            }
            func_decl * f = to_app(e)->get_decl();
            //compute the definition
            if (is_uninterp(f)) {
                simple_def * df = mct->get_simple_def(mc,f);
                if (!var_to_bind.empty()) {
                    var_bind_count += var_to_bind.size();
                    ptr_vector<eval_node> new_active;
                    if (var_bind_count<q->get_num_decls()) {
                        //have each newly bound variable notify that they evaluate
                        for (unsigned j=0; j<var_to_bind.size(); j++) {
                            SASSERT(vsub[var_to_bind[j]]==0);
                            if (vars[var_to_bind[j]]) {
                                vars[var_to_bind[j]]->notify_evaluation(new_active);
                            }
                        }
                        //as well as this
                        en->notify_evaluation(new_active);
                        if (!new_active.empty()) {
                            TRACE("eval_check_debug", 
                                for (unsigned i=0; i<new_active.size(); i++) {
                                    tout << "Now active : " << mk_pp(new_active[i]->get_expr(),m_m) << "\n";
                                }
                                );
                            new_active.append(active.size(), active.c_ptr());
                        }
                    }
                    SASSERT(df->get_else());
                    TRACE("eval_check_debug", tout << "Process definition : "; 
                                              mc.display(tout, df); 
                                              tout << "\n";
                                              tout << "With arguments : \n";
                                                for (unsigned l=0; l<children.size(); l++) {
                                                    tout << "   "; 
                                                    mc.display(tout, children[l]); 
                                                    tout << "\n";
                                                };
                                                tout << "Current entry is : \n";
                                                for (unsigned l=0; l<vsub.size(); l++) {
                                                    if (vsub[l]) { mc.display(tout,vsub[l]); }else{tout << "*";}
                                                    tout << " ";
                                                };
                                                );
                    unsigned process_num_entries = df->get_num_entries();
                    for (unsigned i=0; i<process_num_entries; i++) {
                    //for (unsigned i=0; i<df->get_num_entries(); i++) {
                        annot_entry * cf = df->get_condition(i);
                        en->m_value = df->get_value(i);
                        if (mc.do_compose(vsub, children, esub, cf)) {
                            for (unsigned j=0; j<var_to_bind.size(); j++) {
                                if (vars[var_to_bind[j]]) {
                                    vars[var_to_bind[j]]->m_value = vsub[var_to_bind[j]];
                                }
#ifdef EVAL_CHECK_DEBUG
                                expr * ve = evaluate(mct, esub[(vsub.size()-1)-var_to_bind[j]]);
                                if (ve!=vsub[var_to_bind[j]]) {
                                    TRACE("eval_check_warn", tout << "Bad term : " << mk_pp(esub[(vsub.size()-1)-var_to_bind[j]], m_m) << "\n";
                                                             tout << "Evaluates to "; mc.display(tout,ve); tout << ", not equal to "; mc.display(tout, vsub[var_to_bind[j]]); tout << "\n";);
                                    SASSERT(false);
                                }
#endif
                            }
                            TRACE("eval_check_debug", tout << "Process entry : "; 
                                                        for (unsigned l=0; l<vsub.size(); l++) {
                                                            if (vsub[l]) { mc.display(tout,vsub[l]); }else{tout << "*";}
                                                            tout << " ";
                                                        }
                                                        );
                            //TRACE("eval_check_debug", tout << "Process entry "; mc.display(tout, dc->get_condition(i)); tout << "\n";);
                            if (var_bind_count<q->get_num_decls()) {
                                en->m_value = df->get_value(i);
                                if (new_active.empty()) {
                                    if (en->get_expr()!=q->get_expr() || m_m.is_false(en->m_value)) {
                                        //SASSERT(!active.empty() || en->get_expr()==q->get_expr());
                                        eresult = do_eval_check(mc, mct, q, active, vars, vsub, esub, instantiations, var_bind_count, repaired);
                                        if (eresult==l_false) {
                                            return l_false;
                                        }
                                    }
                                }
                                else {
                                    eresult = do_eval_check(mc, mct, q, new_active, vars, vsub, esub, instantiations, var_bind_count, repaired);
                                    if (eresult==l_false) {
                                        return l_false;
                                    }
                                }
                            }
                            else {
                                TRACE("eval_check_debug", tout << "Add instantiation now.\n";);
                                mc.set_evaluate_cache_active(true);
                                //just do the evaluation now
                                //we have an instantiation
                                for (unsigned k=0; k<vsub.size(); k++) {
                                    SASSERT(vsub[k]);
                                    SASSERT(esub[k]);
                                }
                                if (mc.add_instantiation(mct, q, esub, vsub, instantiations, repaired, true, true, false)) {
                                    eresult = l_true;
                                }
                                mc.set_evaluate_cache_active(false);
                                TRACE("eval_check_debug", tout << "Finished instantiation.\n";);
                            }
                        }

                        //undo the match
                        for (unsigned j=0; j<var_to_bind.size(); j++) {
                            vsub.set(var_to_bind[j], 0);
                            esub.set((vsub.size()-1)-var_to_bind[j], 0);
                        }
                        if (!firstTime && eresult==l_true && m_eval_check_inst_limited) {
                            SASSERT(!instantiations.empty());
                            break;
                        }
                    }
                    if (var_bind_count<q->get_num_decls()) {
                        en->unnotify_evaluation();
                        for (unsigned j=0; j<var_to_bind.size(); j++) {
                            if (vars[var_to_bind[j]]) {
                                vars[var_to_bind[j]]->unnotify_evaluation();
                            }
                        }
                    }
                }
                else {
                    //just evaluate
                    //TRACE("eval_check_debug", tout << "Definition is "; mc.display(tout,df); tout << "\n";);
                    result = df->evaluate(mc, children);
                    //--------------------test
                    /*
                    ptr_buffer<val> vsub;
                    for (unsigned i=0; i<curr_cond->get_size(); i++) {
                        if (curr_cond->get_value(i)->is_value()) {
                            vsub.push_back(to_value(curr_cond->get_value(i))->get_value());
                        }
                        else {
                            vsub.push_back(0);
                        }
                    }
                    result = evaluate(mct, e, vsub, true);
                    */
                    //---------------------end test
                }
            }
            else {
                TRACE("eval_term_debug", tout << "evaluate for " << mk_pp(e, m_m) << "\n";);
                //just evaluate
                result = mc.evaluate_interp(f, children);
            }
        }
        // if processing a simple result, just recurse
        if (result) {
            TRACE("eval_check_debug", tout << "Evaled, lookup got "; mc.display(tout, result); tout << "\n";);
            ptr_vector<eval_node> new_active;
            en->notify_evaluation(new_active);
            en->m_value = result;
            if (new_active.empty()) {
                if (en->get_expr()!=q->get_expr() || m_m.is_false(en->m_value)) {
                    if (active.empty() && en->get_expr()!=q->get_expr()) {
                        SASSERT(var_bind_count<q->get_num_decls());
                        TRACE("eval_check_warn", tout << "WARNING: Evaluation finished and not all variables are bound.\n";);
                        return l_false;
                    }
                    else {
                        eresult = do_eval_check(mc, mct, q, active, vars, vsub, esub, instantiations, var_bind_count, repaired);
                    }
                }
            }
            else {
                TRACE("eval_check_debug", 
                    for (unsigned i=0; i<new_active.size(); i++) {
                        tout << "Now active : " << mk_pp(new_active[i]->get_expr(),m_m) << "\n";
                    }
                    );
                new_active.append(active.size(), active.c_ptr());
                eresult = do_eval_check(mc, mct, q, new_active, vars, vsub, esub, instantiations, var_bind_count, repaired);
            }
            en->m_value = 0;
            en->unnotify_evaluation();
        }
        //put it back into active
        active.push_back(en);
    }
    else {
        SASSERT(var_bind_count<q->get_num_decls());
        TRACE("eval_check_warn", tout << "Did not bind all variables in quantifier " << mk_pp(q,m_m) << "\n";
                                 tout << "WARNING: no terms to evaluate.\n";);
        return l_false;
    }
    SASSERT(eresult==l_false || active.size()==prev_size);
    return eresult;
}
