/*++
Copyright (c) 2013 Microsoft Corporation

Module Name:

    bound_minimize.cpp

Abstract:

    <abstract>

Author:

    Andy Reynolds 2013-01-24.

--*/
#include"bound_minimize.h"
#include"ast_pp.h"

void propagate_bound_info::introduce_var(sort * s, expr_ref & e, bound_propagator::var & var) {
    for (unsigned i = 0; i < m_bp_exprs.size(); i++) {
        if (m_bp_exprs[i]==e) {
            var = m_bp_vars[i];
            return;
        }
    }
    //make variable
    var = static_cast<bound_propagator::var>(m_bp_exprs.size());
    m_bp.mk_var(var, true);
    m_bp_vars.push_back(var);
    m_bp_exprs.push_back(e);
}

void propagate_bound_info::introduce_var(sort * s, expr_ref & x, expr_ref_buffer & terms, scoped_mpq_buffer & coeffs, bound_propagator::var & vvar, bound_propagator::var & bvar) {
    introduce_var(s, x, vvar);
    if (terms.empty()) {
        //just return the variable directly
        bvar = vvar;
    } 
    else {
        //first, ensure that each term has been introduced
        scoped_mpq_buffer as(coeffs.m()); 
        sbuffer<bound_propagator::var> xs;
        as.push_back(mpq(-1));
        xs.push_back(vvar);
        for (unsigned i = 0; i < terms.size(); i++) {
            expr_ref t(m_m);
            t = terms[i];
            bound_propagator::var tvar;
            introduce_var(s, t, tvar);
            as.push_back(coeffs[i]);
            xs.push_back(tvar);
        }
        //introduce new variable
        bvar = static_cast<bound_propagator::var>(m_bp_vars.size());
        m_bp.mk_var(bvar, true);
        m_bp_vars.push_back(bvar);
        m_bp_exprs.push_back(0);
        //add the equation to the bound propagator
        as.push_back(1);
        xs.push_back(bvar);
        TRACE("propagate-bound-info-debug", tout << "Mk eq, size = " << terms.size() << "\n";);
        m_bp.mk_eq(as.size(), as.c_ptr(), xs.c_ptr());
    }
}

bool propagate_bound_info::compute(bound_info& bi) {
    mpq zero(0);
    if (bi.is_normalized()) {   //need this?
        TRACE("propagate-bound-info-debug", tout << "Propagate bound info: Compute for " << mk_pp(bi.m_q,m_m) << "\n";);
        //add equations into bound propagator
        for (unsigned i = 0; i < bi.m_var_order.size(); i++) {
            int index = bi.m_var_order[i];
            sort * s = bi.m_q->get_decl_sort(bi.m_q->get_num_decls()-1-index);
            if (m_au.is_int(s)) {
                expr_ref x(m_m);
                x = m_m.mk_var(index, s);
                expr_ref upper(m_m);
                upper = bi.get_upper_bound(index);
                //must do theory rewriter on upper to process it as a monomial
                TRACE("propagate-bound-info-debug", tout << "Process bound " << mk_pp(upper,m_m) << "\n";);
                //process x <= u into x <= c1*t1 + ... + cn*tn + c
                expr_ref_buffer   terms(m_m);
                scoped_mpq_buffer coeffs(m_bp.nm());
                scoped_mpq        cval(m_bp.nm());
                m_au.get_polynomial(upper, coeffs, terms, cval);
                // introduce variable for v,  v = x - (c1*t1 + ... + cn*tn)
                // this will introduce equation v - x + (c1*t1 + ... + cn*tn) = 0
                bound_propagator::var vv;
                bound_propagator::var bv;
                introduce_var(s, x, terms, coeffs, vv, bv);
                TRACE("propagate_bound_info", 
                      tout << " v" << bv << " <= " << cval << "\n";
                      tout << " v" << vv << " >= 0\n";);
                // add bound 0 <= vv
                m_bp.assert_lower(vv, zero, false);
                // add bound bv <= c
                m_bp.assert_upper(bv, cval, false);
                //record which variable was used for bound
                m_bp_bi_vars.push_back(vv);
                m_bp_bi_bounds.push_back(bv);
            }
            else {
                m_bp_bi_vars.push_back(0);
                m_bp_bi_bounds.push_back(0);
            }
        }
        //propagate the bounds
        TRACE("propagate-bound-info-debug", tout << "Propagate the bounds...\n";);
        m_bp.propagate();
        //if unsatisfiable, we are done
        if (m_bp.inconsistent()) {
            TRACE("propagate-bound-info-debug", tout << "Inconsistent bounds.\n";);
            bi.m_is_trivial_sat = true;
        }
        else {
            //now, see what we got for the bounds 
            for (unsigned i = 0; i < bi.m_var_order.size(); i++) {
                int index = bi.m_var_order[i];
                sort * s = bi.m_q->get_decl_sort(bi.m_q->get_num_decls()-1-index);
                if (m_au.is_int(s)) {
                    bound_propagator::var vv = m_bp_bi_vars[i];
                    //check the bounds found
                    for (unsigned b=0; b<2; b++) {
                        bool isLower = b==0;
                        if ((isLower && (m_bp.has_lower(vv))) || (!isLower && (m_bp.has_upper(vv)))) {
                            rational rb(isLower ? m_bp.lower(vv) : m_bp.upper(vv));
                            expr_ref b(m_m);
                            b = m_au.mk_numeral(rb, true);
                            //std::cout << "Set bound for variable " << index << std::endl;
                            expr_ref_buffer & bnd = isLower ? bi.m_l : bi.m_u;
                            expr_ref curr_b(m_m);
                            curr_b = bnd[index];
                            rational curr_br;
                            if (m_au.is_numeral(curr_b, curr_br)) {
                                //should be geq if isLower, leq is !isLower
                                bnd.setx(index, b);
                            }
                            else {
                                // [Leo]: ???
                                //combine bounds?
                            }
                        }
                    }
                }
            }
        }
        return true;
    }
    else {
        TRACE("propagate-bound-info-debug",tout << "Bounds are not normalized.\n";);
        return false;
    }
}

// [Leo]: we should use display(std::ostream & out) instead of print to std::cout
// [Leo]: What is tc? It is not used.
void propagate_bound_info::print(const char * tc) {
    std::cout << "Propagated bounds :\n";
    for (unsigned i = 0; i < m_bp_vars.size(); i++) {
        bound_propagator::var v = m_bp_vars[i];
        //get upper/lower bounds
        if (m_bp.has_lower(v)) {
            rational rl(m_bp.lower(v));
            expr_ref l(m_m);
            l = m_au.mk_numeral(rl, true);
            std::cout << mk_pp(l,m_m);
        }
        else {
            std::cout << "-[INF]";
        }
        std::cout << " <= " << mk_pp(m_bp_exprs[i], m_m) << " <= ";
        if (m_bp.has_upper(v)) {
            rational ru(m_bp.upper(v));
            expr_ref u(m_m);
            u = m_au.mk_numeral(ru, true);
            std::cout << mk_pp(u,m_m);
        }
        else {
            std::cout << "[INF]";
        }
        std::cout << "\n";
    }
}

bool bv_trans_bound_info::compute(bound_info& bi) {
    //TODO ???
    return false;
}

void bv_trans_bound_info::print( const char * tc ) {

}