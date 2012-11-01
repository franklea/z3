/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    strategic_solver.h

Abstract:

    Strategies -> Solver

Author:

    Leonardo (leonardo) 2011-05-19

Notes:

--*/
#ifndef _STRATEGIC_SOLVER_H_
#define _STRATEGIC_SOLVER_H_

#include"solver.h"
#include"tactic.h"

class progress_callback;
struct front_end_params;

/**
   \brief Implementation of the solver API that supports:
       - a different tactic for each logic 
       - a general purpose tactic
       - a default incremental solver

   The strategic solver has two modes:
       - non-incremental
       - incremental
   In non-incremental mode, tactics are used.
   In incremental model, the incremental (general purpose) solver is used.
   
   A timeout for the incremental solver can be specified.
   If the timeout is reached, then the strategic_solver tries to solve the problem using tactics.

   The strategic_solver switches to incremental when:
       - push is used
       - assertions are peformed after a check_sat
   It goes back to non_incremental mode when:
       - reset is invoked.
*/
class strategic_solver_core : public solver {
public:
    // Behavior when the incremental solver returns unknown.
    enum inc_unknown_behavior {
        IUB_RETURN_UNDEF,      // just return unknown
        IUB_USE_TACTIC_IF_QF,  // invoke tactic if problem is quantifier free
        IUB_USE_TACTIC         // invoke tactic
    };

private:
    ast_manager *        m_manager;
    front_end_params *   m_fparams;
    symbol               m_logic;
    bool                 m_force_tactic; // use tactics even when auto_config = false
    bool                 m_inc_mode;
    bool                 m_check_sat_executed;
    scoped_ptr<solver>   m_inc_solver;
    unsigned             m_inc_solver_timeout;
    inc_unknown_behavior m_inc_unknown_behavior;
    scoped_ptr<tactic_factory>  m_default_fct;
    dictionary<tactic_factory*> m_logic2fct;

    ref<tactic>          m_curr_tactic;
    
    bool                 m_use_inc_solver_results;
    model_ref            m_model;
    proof *              m_proof;
    std::string          m_reason_unknown;
    statistics           m_stats;

#ifdef Z3DEBUG
    unsigned             m_num_scopes;
#endif

    bool                 m_produce_proofs;
    bool                 m_produce_models;
    bool                 m_produce_unsat_cores;

    progress_callback *  m_callback;

    void reset_results();
    void init_inc_solver();
    tactic_factory * get_tactic_factory() const;
    lbool check_sat_with_assumptions(unsigned num_assumptions, expr * const * assumptions);

    struct mk_tactic;

    bool has_quantifiers() const;
    bool use_tactic_when_undef() const;

public:
    strategic_solver_core();
    ~strategic_solver_core();

    ast_manager & m() const { SASSERT(m_manager); return *m_manager; }

    void set_inc_solver(solver * s);
    void set_inc_solver_timeout(unsigned timeout);
    void set_default_tactic(tactic_factory * fct);
    void set_tactic_for(symbol const & logic, tactic_factory * fct);
    void set_inc_unknown_behavior(inc_unknown_behavior b) { m_inc_unknown_behavior = b; }
    void force_tactic(bool f) { m_force_tactic = f; }

    virtual void set_front_end_params(front_end_params & p) { m_fparams = &p; }

    virtual void updt_params(params_ref const & p);
    virtual void collect_param_descrs(param_descrs & r);

    virtual void set_produce_proofs(bool f) { m_produce_proofs = f; }
    virtual void set_produce_models(bool f) { m_produce_models = f; }
    virtual void set_produce_unsat_cores(bool f) { m_produce_unsat_cores = f; }

    virtual unsigned get_num_assertions() const = 0;
    virtual expr * get_assertion(unsigned idx) const = 0;
    
    virtual void display(std::ostream & out) const;
    
    virtual void init(ast_manager & m, symbol const & logic);
    virtual void collect_statistics(statistics & st) const;
    virtual void reset();
    virtual void assert_expr(expr * t);
    virtual void push();
    virtual void pop(unsigned n);
    virtual unsigned get_scope_level() const;
    virtual lbool check_sat(unsigned num_assumptions, expr * const * assumptions);
    virtual void get_unsat_core(ptr_vector<expr> & r);
    virtual void get_model(model_ref & m);
    virtual proof * get_proof();
    virtual std::string reason_unknown() const;
    virtual void get_labels(svector<symbol> & r);
    virtual void set_cancel(bool f);
    virtual void set_progress_callback(progress_callback * callback);
};

/**
   \brief Default implementation of strategic_solver_core
*/
class strategic_solver : public strategic_solver_core {
    struct ctx {
        expr_ref_vector              m_assertions;
        unsigned_vector              m_scopes;
        ctx(ast_manager & m);
    };
    scoped_ptr<ctx>            m_ctx;
public:
    strategic_solver() {}

    virtual void init(ast_manager & m, symbol const & logic);

    virtual void assert_expr(expr * t);
    virtual void push();
    virtual void pop(unsigned n);
    virtual void reset();

    virtual unsigned get_num_assertions() const;
    virtual expr * get_assertion(unsigned idx) const;
};



#endif
