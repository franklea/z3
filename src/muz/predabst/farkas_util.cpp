/*++
Copyright (c) 2013 Microsoft Corporation

Module Name:

    farkas_util.cpp

Abstract:

    Utilities for applying farkas lemma over linear implications.

Author:

    Tewodros A. Beyene (t-tewbe) 2014-10-22.

Revision History:

--*/
#include "farkas_util.h"
#include "predabst_util.h"
#include "well_sorted.h"
#include "th_rewriter.h"
#include "arith_decl_plugin.h"
#include "ast_pp.h"
#include "smt_kernel.h"
#include "smt_params.h"
#include "scoped_proof.h"
#include "iz3mgr.h"

std::ostream& operator<<(std::ostream& out, rel_op op) {
    switch (op) {
    case op_eq:
        out << "=";
        break;
    case op_le:
        out << "<=";
        break;
    default:
        UNREACHABLE();
        break;
    }
    return out;
}

// Returns the number of lambdas of kind 'bilinear' or 'bilinear_single'
// that are still uninterpreted constants (i.e. which haven't been
// substituted for a specific value).
static unsigned count_bilinear_uninterp_const(vector<lambda_info> const& lambdas) {
    unsigned num_bilinear_uninterp_const = 0;
    for (unsigned i = 0; i < lambdas.size(); i++) {
        if (((lambdas[i].m_kind == bilinear) || (lambdas[i].m_kind == bilinear_single)) &&
            is_uninterp_const(lambdas[i].m_lambda)) {
            ++num_bilinear_uninterp_const;
        }
    }
    return num_bilinear_uninterp_const;
}

// Converts an integer (in)equality (E1 op E2) to the form (E' op' 0),
// where op' is either = or <=.  Returns false if the expression is not a
// binary integer (in)equality.
static bool leftify_inequality(expr_ref const& e, expr_ref& new_e, rel_op& new_op) {
    ast_manager& m = e.m();
    arith_util arith(m);
    CASSERT("predabst", is_well_sorted(m, e));

    expr *e1;
    expr *e2;
    if (m.is_eq(e, e1, e2)) {
        // (e1 == e2) <=> (e1 - e2 == 0)
        new_e = arith.mk_sub(e1, e2);
        new_op = op_eq;
    }
    else if (arith.is_le(e, e1, e2)) {
        // (e1 <= e2) <=> (e1 - e2 <= 0)
        new_e = arith.mk_sub(e1, e2);
        new_op = op_le;
    }
    else if (arith.is_ge(e, e1, e2)) {
        // (e1 >= e2) <=> (e2 - e1 <= 0)
        new_e = arith.mk_sub(e2, e1);
        new_op = op_le;
    }
    else if (arith.is_lt(e, e1, e2)) {
        // (e1 < e2) <=> (e1 - e2 + 1 <= 0)
        new_e = arith.mk_add(arith.mk_sub(e1, e2), arith.mk_numeral(rational::one(), true));
        new_op = op_le;
    }
    else if (arith.is_gt(e, e1, e2)) {
        // (e1 > e2) <=> (e2 - e1 + 1 <= 0)
        new_e = arith.mk_add(arith.mk_sub(e2, e1), arith.mk_numeral(rational::one(), true));
        new_op = op_le;
    }
    else {
        STRACE("predabst", tout << "Expression is not a binary (in)equality: " << mk_pp(e, m) << "\n";);
        return false;
    }

    if (!sort_is_int(e1, m)) {
        STRACE("predabst", tout << "Operands of (in)equality are not integers: " << mk_pp(e, m) << "\n";);
        return false;
    }

    CASSERT("predabst", sort_is_int(e2, m));
    CASSERT("predabst", is_well_sorted(m, new_e));
    CASSERT("predabst", sort_is_int(new_e, m));
    return true;
}

expr_ref make_linear_combination(vector<int64> const& coeffs, expr_ref_vector const& inequalities) {
    CASSERT("predabst", coeffs.size() == inequalities.size());
    ast_manager& m = inequalities.m();
    arith_util arith(m);
    expr_ref_vector terms(m);
    bool equality = true;
    for (unsigned i = 0; i < inequalities.size(); ++i) {
        expr_ref new_e(m);
        rel_op new_op;
        bool result = leftify_inequality(expr_ref(inequalities[i], m), new_e, new_op);
        CASSERT("predasbst", result); // >>> why?
        terms.push_back(arith.mk_mul(arith.mk_numeral(rational(coeffs[i], rational::i64()), true), new_e));
        CASSERT("predabst", (new_op == op_eq) || (new_op == op_le));
        if (new_op == op_le) {
            equality = false;
        }
    }
    expr_ref lhs = mk_sum(terms);
    expr_ref rhs(arith.mk_numeral(rational::zero(), true), m);
    return expr_ref(equality ? m.mk_eq(lhs, rhs) : arith.mk_le(lhs, rhs), m);
}

class linear_inequality {
    // Represents a linear integer (in)equality in the variables m_vars.
    //
    // Specifically, represents the (in)equality:
    //     (Sigma_i (m_vars[i] * m_coeffs[i])) m_op m_const
    // where m_vars are distinct variables, and m_coeffs and
    // m_const do not contain any of those variables.

    expr_ref_vector const m_vars;
    expr_ref_vector m_coeffs;
    rel_op m_op;
    expr_ref m_const;

    bool m_has_params; // true if m_coeffs or m_const contain any uninterpreted constants
    
    ast_manager& m;

public:
    linear_inequality(expr_ref_vector const& vars) :
        m_vars(vars),
        m_coeffs(vars.get_manager()),
        m_const(vars.get_manager()),
        m(vars.get_manager()) {
        for (unsigned i = 0; i < vars.size(); ++i) {
            CASSERT("predabst", is_var(vars[i]) || is_uninterp_const(vars[i]));
            CASSERT("predabst", sort_is_int(vars[i], m));
        }
    }

    // Initializes this object from an expression representing a (binary)
    // linear integer (in)equality.  Returns false if this is impossible,
    // i.e. if the expression is not a (binary) (in)equality, if the
    // operands are not integers, or if they are not linear in m_vars.
    bool set(expr_ref const& e) {
        CASSERT("predabst", is_well_sorted(m, e));
        arith_util arith(m);
        th_rewriter rw(m);

        m_coeffs.reset();
        m_const.reset();
        m_has_params = false;

        // Push all terms to the LHS of the (in)equality.
        expr_ref lhs(m);
        bool result = leftify_inequality(e, lhs, m_op);
        if (!result) {
            return false;
        }

        // Simplify the LHS of the (in)equality.  The simplified expression
        // will be a sum of terms, each of which is a product of factors.
        rw(lhs);

        // Split the terms into those which have one of the m_vars as a
        // factor (var_terms), and those which do not (const_terms), while
        // checking that all the terms are linear in m_vars.
        vector<expr_ref_vector> var_terms;
        var_terms.reserve(m_vars.size(), expr_ref_vector(m));
        expr_ref_vector const_terms(m);

        expr_ref_vector terms = get_additive_terms(lhs);
        for (unsigned i = 0; i < terms.size(); ++i) {
            expr_ref term(terms.get(i), m);
            
            // Split the factors into those which are one of the m_vars
            // (var_factors) and those which are not (const_factors).
            expr_ref_vector var_factors(m);
            expr_ref_vector const_factors(m);

            expr_ref_vector factors = get_multiplicative_factors(term);
            for (unsigned j = 0; j < factors.size(); ++j) {
                expr_ref factor(factors.get(j), m);
                if (m_vars.contains(factor)) {
                    var_factors.push_back(factor);
                }
                else {
                    expr_ref_vector factor_vars = get_all_vars(factor);
                    for (unsigned k = 0; k < factor_vars.size(); ++k) {
                        if (m_vars.contains(factor_vars.get(k))) {
                            STRACE("predabst", tout << "Found non-linear factor " << mk_pp(factor, m) << "\n";);
                            return false;
                        }
                    }
                    if (!factor_vars.empty()) {
                        m_has_params = true;
                    }
                    else {
                        CASSERT("predabst", arith.is_numeral(factor));
                    }
                    const_factors.push_back(factor);
                }
            }

            // Classify the term based on the number of var_factors it
            // contains.
            if (var_factors.size() == 0) {
                const_terms.push_back(term);
            }
            else if (var_factors.size() == 1) {
                unsigned j = vector_find(m_vars, var_factors.get(0));
                var_terms[j].push_back(mk_prod(const_factors));
            }
            else {
                STRACE("predabst", tout << "Found non-linear term " << mk_pp(term, m) << "\n";);
                return false;
            }
        }

        // Move the constant terms to the RHS of the (in)equality.
        m_const = arith.mk_uminus(mk_sum(const_terms));
        STRACE("predabst", tout << "m_const before rewrite: " << mk_pp(m_const, m) << "\n";);
        rw(m_const);
        STRACE("predabst", tout << "m_const after rewrite: " << mk_pp(m_const, m) << "\n";);

        for (unsigned i = 0; i < m_vars.size(); ++i) {
            m_coeffs.push_back(mk_sum(var_terms.get(i)));
        }

        return true;
    }

    expr_ref get_coeff(unsigned i) const {
        return expr_ref(m_coeffs.get(i), m);
    }

    rel_op get_op() const {
        return m_op;
    }

    expr_ref get_const() const {
        return m_const;
    }

    bool has_params() const {
        return m_has_params;
    }

    expr_ref to_expr() const {
        arith_util arith(m);
        expr_ref_vector lhs_terms(m);
        expr_ref_vector rhs_terms(m);

        for (unsigned i = 0; i < m_vars.size(); ++i) {
            expr* coeff = m_coeffs.get(i);
            rational val;
            bool is_int;
            bool result = arith.is_numeral(coeff, val, is_int);
            CASSERT("predabst", result);
            CASSERT("predabst", is_int);
            if (val.is_pos()) {
                if (val.is_one()) {
                    lhs_terms.push_back(m_vars.get(i));
                }
                else {
                    lhs_terms.push_back(arith.mk_mul(coeff, m_vars.get(i)));
                }
            }
            else if (val.is_neg()) {
                if (val.is_minus_one()) {
                    rhs_terms.push_back(m_vars.get(i));
                }
                else {
                    expr_ref neg_coeff(arith.mk_numeral(-val, is_int), m);
                    rhs_terms.push_back(arith.mk_mul(neg_coeff, m_vars.get(i)));
                }
            }
            else {
                CASSERT("predabst", val.is_zero());
            }
        }

        // Prefer X + Y >= Z to Z <= X + Y, but prefer X + Y <= Z + W to Z + W >= X + Y.
        bool swap = (rhs_terms.size() > lhs_terms.size());
        bool strict = false;

        rational val;
        bool is_int;
        bool result = arith.is_numeral(m_const, val, is_int);
        CASSERT("predabst", result);
        CASSERT("predabst", is_int);
        if (val.is_pos()) {
            rhs_terms.push_back(m_const);
        }
        else if (val.is_neg()) {
            if ((m_op == op_le) && val.is_minus_one() && !lhs_terms.empty()) {
                // Prefer X < Y to X + 1 <= Y, but prefer X <= 1 to X < 0.
                strict = true;
            }
            else {
                expr_ref neg_const(arith.mk_numeral(-val, is_int), m);
                lhs_terms.push_back(neg_const);
            }
        }
        else {
            CASSERT("predabst", val.is_zero());
        }

        // Prefer X + Y + C <= Z + W to Z + W <= X + Y + C.
        swap |= (rhs_terms.size() > lhs_terms.size());

        expr_ref lhs = mk_sum(lhs_terms);
        expr_ref rhs = mk_sum(rhs_terms);
        return expr_ref(m_op == op_eq ? (swap ? m.mk_eq(rhs, lhs) : m.mk_eq(lhs, rhs)) :
            strict ? (swap ? arith.mk_gt(rhs, lhs) : arith.mk_lt(lhs, rhs)) :
            (swap ? arith.mk_ge(rhs, lhs) : arith.mk_le(lhs, rhs)), m);
    }

    friend std::ostream& operator<<(std::ostream& out, linear_inequality const& lineq);
};

std::ostream& operator<<(std::ostream& out, linear_inequality const& lineq) {
    ast_manager& m = lineq.m;
    for (unsigned i = 0; i < lineq.m_vars.size(); ++i) {
        if (i != 0) {
            out << " + ";
        }
        out << mk_pp(lineq.m_coeffs[i], m) << " * " << mk_pp(lineq.m_vars[i], m);
    }
    out << " " << lineq.m_op << " " << mk_pp(lineq.m_const, m);
    if (lineq.m_has_params) {
        out << " (has params)";
    }
    return out;
}

class farkas_imp {
    // Represents an implication from a set of linear (in)equalities to
    // another linear inequality, where all the (in)equalities are linear
    // in a common set of variables (m_vars).
    //
    // Symbolically:
    //
    //   (A . v <= b) ==> (c . v <= d)
    //
    // Or graphically:
    //
    //   (. . .) (.)    (.)               (.)
    //   (. A .) (v) <= (b)  ==>  (. c .) (v) <= d
    //   (. . .) (.)    (.)               (.)
    //
    // Farkas's lemma says that this implication holds if and only if
    // the inequality on the RHS is a consequence of a linear combination
    // of the inequalities on the LHS, where the multipliers of all
    // inequalities on the LHS must be non-negative.  That is:
    //
    //     Forall v, (A . v <= b) ==> (c . v <= d)
    //   <==>
    //     Exists lambda, (lambda >= 0), (lambda . A = c) AND (lambda . b <= d)
    //
    // If any of (in)equalities on the LHS are actually equalities,
    // then the constraint on that lambda may be dropped.

    expr_ref_vector const m_vars;
    vector<linear_inequality> m_lhs;
    linear_inequality m_rhs;
    expr_ref_vector m_lambdas;
    unsigned m_num_bilinear;

    ast_manager& m;

public:
    farkas_imp(expr_ref_vector const& vars) :
        m_vars(vars),
        m_rhs(vars),
        m_lambdas(vars.get_manager()),
        m(vars.get_manager()) {
    }

    // Initializes this object from a set of LHS expressions and one RHS
    // expression.  Returns false if any of the LHS expressions is not a
    // (binary) linear integer (in)equality, or if the RHS is not a linear
    // integer inequality.
    bool set(expr_ref_vector const& lhs_es, expr_ref const& rhs_e) {
        STRACE("predabst", tout << "Solving " << lhs_es << " => " << mk_pp(rhs_e, m) << ", in variables " << m_vars << "\n";);
        m_lhs.reset();
        for (unsigned i = 0; i < lhs_es.size(); ++i) {
            m_lhs.push_back(linear_inequality(m_vars));
            bool result = m_lhs[i].set(expr_ref(lhs_es.get(i), m));
            if (!result) {
                STRACE("predabst", tout << "LHS[" << i << "] is not a linear integer (in)equality\n";);
                return false;
            }
        }

        bool result = m_rhs.set(rhs_e);
        if (!result) {
            STRACE("predabst", tout << "RHS is not a linear integer (in)equality\n";);
            return false;
        }

        if (m_rhs.get_op() == op_eq) {
            STRACE("predabst", tout << "RHS is an equality not an inequality\n";);
            return false;
        }

        m_lambdas.swap(make_lambdas(m_num_bilinear));
        return true;
    }

    // Returns a collection of constraints whose simultaneous satisfiability
    // is equivalent to the validity of the implication represented by this
    // object.  A solution to these constraints will assign a value to each
    // of the multipliers (returned by get_lambdas() (below)) that will enable
    // the RHS inequality to be derived from the LHS (in)equalities.
    expr_ref_vector get_constraints() const {
        arith_util arith(m);

        expr_ref_vector constraints(m);

        // The multipliers for all inequalities (as opposed to equalities)
        // must be non-negative.
        for (unsigned j = 0; j < m_lhs.size(); ++j) {
            expr* lambda = m_lambdas.get(j);
            rel_op op = m_lhs.get(j).get_op();
            CASSERT("predabst", (op == op_le) || (op == op_eq));
            if (op == op_le) {
                if (!arith.is_one(lambda)) {
                    constraints.push_back(arith.mk_ge(lambda, arith.mk_numeral(rational::zero(), true)));
                }
            }
        }

        // lambda . A = c
        for (unsigned i = 0; i < m_vars.size(); ++i) {
            expr_ref_vector terms(m);
            for (unsigned j = 0; j < m_lhs.size(); ++j) {
                expr* lambda = m_lambdas.get(j);
                expr* coeff = m_lhs.get(j).get_coeff(i);
                if (!arith.is_zero(coeff)) {
                    if (arith.is_one(lambda)) {
                        terms.push_back(coeff);
                    }
                    else {
                        terms.push_back(arith.mk_mul(lambda, coeff));
                    }
                }
            }
            constraints.push_back(m.mk_eq(mk_sum(terms), m_rhs.get_coeff(i)));
        }

        // lambda . b <= d
        expr_ref_vector terms(m);
        for (unsigned j = 0; j < m_lhs.size(); ++j) {
            expr* lambda = m_lambdas.get(j);
            expr* constant = m_lhs.get(j).get_const();
            if (!arith.is_zero(constant)) {
                terms.push_back(arith.mk_mul(lambda, constant));
            }
        }
        constraints.push_back(arith.mk_le(mk_sum(terms), m_rhs.get_const()));

        return constraints;
    }

    // Returns a list of objects, in 1-to-1 correspondance with the LHS
    // (in)equalities, that describe the multipliers of those (in)equalities
    // used to derive the RHS inequality.
    vector<lambda_info> get_lambdas() const {
        vector<lambda_info> lambdas;
        for (unsigned i = 0; i < m_lhs.size(); ++i) {
            lambda_kind kind = m_lhs.get(i).has_params() ? (m_num_bilinear == 1 ? bilinear_single : bilinear) : linear;
            lambdas.push_back(lambda_info(expr_ref(m_lambdas.get(i), m), kind, m_lhs.get(i).get_op()));
        }
        return lambdas;
    }

    void display(std::ostream& out) const {
        out << "LHS:\n";
        for (unsigned i = 0; i < m_lhs.size(); ++i) {
            out << "  " << mk_pp(m_lambdas.get(i), m) << ": " << m_lhs.get(i) << std::endl;
        }
        out << "RHS:\n";
        out << "  " << m_rhs << std::endl;
    }

private:
    expr_ref_vector make_lambdas(unsigned& num_bilinear) const {
        // We classify the (in)equalities on the LHS as either 'linear' or
        // 'bilinear', according to whether the coefficients are all
        // integers, or whether some of the coefficients are uninterpreted
        // constants.
        num_bilinear = 0;
        for (unsigned i = 0; i < m_lhs.size(); ++i) {
            if (m_lhs.get(i).has_params()) {
                ++num_bilinear;
            }
        }

        expr_ref_vector lambdas(m);
        for (unsigned i = 0; i < m_lhs.size(); ++i) {
            if ((num_bilinear == 1) && m_lhs.get(i).has_params() && (m_lhs.get(i).get_op() == op_le)) {
                // If this is the sole bilinear (in)equality, and it is an
                // inequality, then without loss of generality we may choose
                // its multiplier to be 1.  (We cannot do this if it is an
                // equality, since the multiplier may need to be negative.)
                // This optimization reduces the problem to a purely linear
                // one.
                arith_util arith(m);
                lambdas.push_back(arith.mk_numeral(rational::one(), true));
            }
            else {
                lambdas.push_back(m.mk_fresh_const("t", arith_util(m).mk_int()));
            }
        }
        return lambdas;
    }
};

// Converts a formula (Forall v, F) to an equivalent formula
// (Exists lambda, F'), where F is a formula in variables (v, p) and
// F' is a formula in variable (lambda, p), and:
//   Forall p, ((Forall v, F) <=> (Exists lambda, F'))
// In particular:
//   (Exists p, Forall v, F) <=> (Exists p, Exists lambda, F')
// the right hand side of which is of a suitable form to be answered by
// an SMT solver.
//
// The implementation uses Farkas's lemma; therefore it will fail (returning
// false) if the atomic boolean formulae in F are not all linear integer
// (in)equalities.
bool mk_exists_forall_farkas(expr_ref const& fml, expr_ref_vector const& vars, expr_ref_vector& constraints, vector<lambda_info>& lambdas, bool eliminate_unsat_disjuncts) {
    ast_manager& m = fml.m();
    arith_util arith(m);
    CASSERT("predabst", is_well_sorted(m, fml));
    CASSERT("predabst", sort_is_bool(fml, m));
    CASSERT("predabst", is_ground(fml));
    CASSERT("predabst", constraints.empty());
    CASSERT("predabst", lambdas.empty());
    for (unsigned i = 0; i < vars.size(); ++i) {
        CASSERT("predabst", is_uninterp_const(vars.get(i)));
        if (!sort_is_int(vars.get(i), m)) {
            STRACE("predabst", tout << "Cannot apply Farkas's lemma: variable " << i << " is of non-integer type\n";);
            return false;
        }
    }
    expr_ref false_ineq(arith.mk_le(arith.mk_numeral(rational::one(), true), arith.mk_numeral(rational::zero(), true)), m);
    // P <=> (not P => false)
    expr_ref norm_fml = to_dnf(expr_ref(m.mk_not(fml), m));
    // ((P1 or ... or Pn) => false) <=> (P1 => false) and ... and (Pn => false)
    expr_ref_vector disjs = get_disj_terms(norm_fml);
    for (unsigned i = 0; i < disjs.size(); ++i) {
        expr_ref_vector conjs = get_conj_terms(expr_ref(disjs.get(i), m));
        if (eliminate_unsat_disjuncts) {
            smt_params new_param;
            new_param.m_model = false;
            smt::kernel solver(fml.m(), new_param);
            for (unsigned j = 0; j < conjs.size(); ++j) {
                solver.assert_expr(conjs.get(j));
            }
            if (solver.check() != l_true) {
                continue;
            }
        }
        farkas_imp f_imp(vars);
        bool result = f_imp.set(conjs, false_ineq);
        if (!result) {
            return false;
        }
        STRACE("predabst", f_imp.display(tout););
        constraints.append(f_imp.get_constraints());
        lambdas.append(f_imp.get_lambdas());
    }
    return true;
}

bool get_farkas_coeffs(proof_ref const& pr, vector<int64>& coeffs) {
    CASSERT("predabst", coeffs.empty());
    ast_manager& m = pr.m();
    iz3mgr i(m);
    iz3mgr::ast p(&m, pr.get());
    iz3mgr::pfrule dk = i.pr(p);
    if (dk == PR_TH_LEMMA &&
        i.get_theory_lemma_theory(p) == iz3mgr::ArithTheory &&
        i.get_theory_lemma_kind(p) == iz3mgr::FarkasKind) {
        std::vector<rational> rat_coeffs;
        i.get_farkas_coeffs(p, rat_coeffs);
        for (unsigned i = 0; i < rat_coeffs.size(); ++i) {
            coeffs.push_back(rat_coeffs[i].get_int64());
        }
        STRACE("predabst", tout << "Proof kind is Farkas\n";);
        return true;
    }
    else {
        STRACE("predabst", tout << "Proof kind is not Farkas\n";);
        return false;
    }
}

bool get_farkas_coeffs_directly(expr_ref_vector const& assertions, vector<int64>& coeffs) {
    CASSERT("predabst", coeffs.empty());
    ast_manager& m = assertions.m();
    scoped_proof sp(m);
    smt_params new_param;
    new_param.m_model = false;
    smt::kernel solver(m, new_param);
    for (unsigned i = 0; i < assertions.size(); ++i) {
        solver.assert_expr(assertions.get(i));
    }
    lbool result = solver.check();
    CASSERT("predabst", result == l_false);
    proof_ref pr(solver.get_proof(), m);
    return get_farkas_coeffs(pr, coeffs);
}

bool get_farkas_coeffs_via_dual(expr_ref_vector const& assertions, vector<int64>& coeffs) {
    CASSERT("predabst", coeffs.empty());
    ast_manager& m = assertions.m();
    arith_util arith(m);
    farkas_imp f_imp(get_all_vars(mk_conj(assertions)));
    expr_ref false_ineq(arith.mk_le(arith.mk_numeral(rational::one(), true), arith.mk_numeral(rational::zero(), true)), m);
    bool result = f_imp.set(assertions, false_ineq);
    if (!result) {
        return false;
    }
    STRACE("predabst", f_imp.display(tout););
    smt_params new_param;
    smt::kernel solver(m, new_param);
    expr_ref_vector constraints = f_imp.get_constraints();
    for (unsigned i = 0; i < constraints.size(); ++i) {
        solver.assert_expr(constraints.get(i));
    }
    lbool lresult = solver.check();
    CASSERT("predabst", lresult == l_true);
    model_ref modref;
    solver.get_model(modref);
    CASSERT("predabst", modref);
    vector<lambda_info> lambdas = f_imp.get_lambdas();
    CASSERT("predabst", lambdas.size() == assertions.size());
    for (unsigned i = 0; i < lambdas.size(); ++i) {
        expr_ref e(m);
        bool result = modref->eval(lambdas.get(i).m_lambda, e);
        CASSERT("predabst", result);
        rational coeff;
        bool is_int;
        result = arith.is_numeral(e, coeff, is_int);
        CASSERT("predabst", result);
        CASSERT("predabst", is_int);
        coeffs.push_back(coeff.get_int64());
    }
    return true;
}

bool get_farkas_coeffs(expr_ref_vector const& assertions, vector<int64>& coeffs) {
    return /* get_farkas_coeffs_directly(assertions, coeffs) || >>> */
        get_farkas_coeffs_via_dual(assertions, coeffs);
}

void well_founded_bound_and_decrease(expr_ref_vector const& vsws, expr_ref& bound, expr_ref& decrease) {
    ast_manager& m = vsws.get_manager();
    arith_util arith(m);
    CASSERT("predabst", vsws.size() % 2 == 0);

    expr_ref_vector vs(m);
    for (unsigned i = 0; i < (vsws.size() / 2); i++) {
        vs.push_back(vsws.get(i));
    }

    expr_ref_vector ws(m);
    for (unsigned i = (vsws.size() / 2); i < vsws.size(); ++i) {
        ws.push_back(vsws.get(i));
    }

    expr_ref_vector sum_psvs_terms(m);
    expr_ref_vector sum_psws_terms(m);
    for (unsigned i = 0; i < vs.size(); ++i) {
        expr_ref param(m.mk_fresh_const("p", arith.mk_int()), m);
        CASSERT("predabst", sort_is_int(vs.get(i), m));
        sum_psvs_terms.push_back(arith.mk_mul(param, vs.get(i)));
        CASSERT("predabst", sort_is_int(ws.get(i), m));
        sum_psws_terms.push_back(arith.mk_mul(param, ws.get(i)));
    }
    expr_ref sum_psvs = mk_sum(sum_psvs_terms);
    expr_ref sum_psws = mk_sum(sum_psws_terms);

    expr_ref delta0(m.mk_const(symbol("delta0"), arith.mk_int()), m);

    bound = arith.mk_ge(sum_psvs, delta0);
    STRACE("predabst", tout << "WF bound: " << mk_pp(bound, m) << "\n";);
    CASSERT("predabst", is_well_sorted(m, bound));

    decrease = arith.mk_lt(sum_psws, sum_psvs);
    STRACE("predabst", tout << "WF decrease: " << mk_pp(decrease, m) << "\n";);
    CASSERT("predabst", is_well_sorted(m, decrease));
}

bool well_founded(expr_ref_vector const& vsws, expr_ref const& lhs, expr_ref* sol_bound, expr_ref* sol_decrease) {
    ast_manager& m = lhs.get_manager();
    CASSERT("predabst", vsws.size() % 2 == 0);
    CASSERT("predabst", sort_is_bool(lhs, m));
    CASSERT("predabst", (sol_bound && sol_decrease) || (!sol_bound && !sol_decrease));

    if (!(m.is_and(lhs) && to_app(lhs)->get_num_args() >= 2)) {
        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is not well-founded: it is not a conjunction of at least 2 terms\n";);
        // XXX very dubious claim...
        return false;
    }

    expr_ref_vector lhs_vars = get_all_vars(lhs);

    // Note that the following two optimizations are valid only if the formula
    // is satisfiable, but we're assuming that is the case.
    bool hasv = false;
    for (unsigned i = 0; i < (vsws.size() / 2); i++) {
        if (lhs_vars.contains(vsws.get(i))) {
            hasv = true;
            break;
        }
    }
    if (!hasv) {
        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is not well-founded: it contains no variable from vs\n";);
        return false;
    }

    bool hasw = false;
    for (unsigned i = (vsws.size() / 2); i < vsws.size(); ++i) {
        if (lhs_vars.contains(vsws.get(i))) {
            hasw = true;
            break;
        }
    }
    if (!hasw) {
        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is not well-founded: it contains no variable from ws\n";);
        return false;
    }

    expr_ref bound(m);
    expr_ref decrease(m);
    well_founded_bound_and_decrease(vsws, bound, decrease);
    expr_ref to_solve(m.mk_or(m.mk_not(lhs), m.mk_and(bound, decrease)), m);

    expr_ref_vector all_vars(vsws);
    for (unsigned j = 0; j < lhs_vars.size(); j++) {
        if (!vsws.contains(lhs_vars.get(j))) {
            all_vars.push_back(lhs_vars.get(j));
        }
    }

    // XXX Does passing true for eliminate_unsat_disjuncts help in the refinement case?
    expr_ref_vector constraints(m);
    vector<lambda_info> lambdas;
    bool result = mk_exists_forall_farkas(to_solve, all_vars, constraints, lambdas);
    if (!result) {
        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is not (provably) well-founded: it does not comprise only linear integer (in)equalities\n";);
        // XXX We need to distinguish between this case and where we have proven that the formula is not well-founded, or else we can end up returning UNSAT incorrectly
        return false;
    }
    CASSERT("predabst", count_bilinear_uninterp_const(lambdas) == 0);

    smt_params new_param;
    if (!sol_bound && !sol_decrease) {
        new_param.m_model = false;
    }
    smt::kernel solver(m, new_param);
    for (unsigned i = 0; i < constraints.size(); ++i) {
        solver.assert_expr(constraints.get(i));
    }

    if (solver.check() != l_true) {
        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is not well-founded: constraint is unsatisfiable\n";);
        return false;
    }

    if (sol_bound && sol_decrease) {
        model_ref modref;
        solver.get_model(modref);
        if (!(modref->eval(bound, *sol_bound) && modref->eval(decrease, *sol_decrease))) {
            return false;
        }

        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is well-founded, with bound " << mk_pp(*sol_bound, m) << "; decrease " << mk_pp(*sol_decrease, m) << "\n";);
    }
    else {
        STRACE("predabst", tout << "Formula " << mk_pp(lhs, m) << " is well-founded\n";);
    }
    
    return true;
}

expr_ref_vector mk_bilinear_lambda_constraints(vector<lambda_info> const& lambdas, int max_lambda, ast_manager& m) {
    arith_util arith(m);

    expr_ref one(arith.mk_numeral(rational::one(), true), m);
    expr_ref minus_one(arith.mk_numeral(rational::minus_one(), true), m);

    expr_ref_vector constraints(m);
    for (unsigned i = 0; i < lambdas.size(); i++) {
        if (lambdas[i].m_kind == bilinear_single) {
            if (lambdas[i].m_op == op_eq) {
                // This is the sole bilinear (in)equality, and it is an
                // equality, so without loss of generality we may choose
                // its multiplier to be either 1 or -1.
                CASSERT("predabst", is_uninterp_const(lambdas[i].m_lambda));
                constraints.push_back(m.mk_or(m.mk_eq(lambdas[i].m_lambda, minus_one), m.mk_eq(lambdas[i].m_lambda, one)));
            }
            else {
                // This is the sole bilinear (in)equality, and it is an
                // inequality, so the multiplier will have been set to 1
                // above and therefore no constraint is necessary.
                CASSERT("predabst", arith.is_one(lambdas[i].m_lambda));
            }
        }
        else if (lambdas[i].m_kind == bilinear) {
            // There is more than one bilinear (in)equality.  In order to
            // make solving tractable, we assume that the multiplier is
            // between 0 and N for an inequality, or -N and N for an
            // equality.  Note that this assumption might prevent us from
            // finding a solution.
            CASSERT("predabst", is_uninterp_const(lambdas[i].m_lambda));
            int min_lambda = (lambdas[i].m_op == op_eq) ? -max_lambda : 0;
            expr_ref_vector bilin_disj_terms(m);
            for (int j = min_lambda; j <= max_lambda; j++) {
                bilin_disj_terms.push_back(m.mk_eq(lambdas[i].m_lambda, arith.mk_numeral(rational(j), true)));
            }
            constraints.push_back(mk_disj(bilin_disj_terms));
        }
    }
    return constraints;
}

expr_ref normalize_pred(expr_ref const& e, var_ref_vector const& vars) {
    ast_manager& m = e.m();
    STRACE("predabst", tout << "Normalizing: " << mk_pp(e, m) << " -> ";);
    th_rewriter tw(m);
    expr_ref e2 = e;
    tw(e2); // >>> convert 0 = 0 to true, etc.
    e2 = to_nnf(e2); // >>> bit of a hack; eliminates 'not'; but also turns not (x=y) into a disjunction, which we probably don't want here.
    for (unsigned i = 0; i < vars.size(); ++i) {
        if (!sort_is_int(vars.get(i), m)) {
            STRACE("predabst", tout << mk_pp(e2, m) << "\n";);
            return e2;
        }
    }
    expr_ref_vector expr_vars(m, vars.size(), (expr* const*)vars.c_ptr());
    linear_inequality ineq(expr_vars);
    if (ineq.set(e2)) {
        e2 = ineq.to_expr();
    }
    STRACE("predabst", tout << mk_pp(e2, m) << "\n";);
    return e2;
}
