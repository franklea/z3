/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    mcsat_solver.h

Abstract:

    MCSAT solver.

Author:

    Leonardo de Moura (leonardo) 2012-11-01.

Revision History:

--*/
#ifndef _MCSAT_SOLVER_H_
#define _MCSAT_SOLVER_H_

#include"solver.h"
class tactic_factory;

namespace mcsat {
    class plugin;

    class solver_factory : public ::solver_factory {
        struct imp;
        imp * m_imp;
    public:
        solver_factory();
        virtual ~solver_factory();
        void add_tactic_before(tactic_factory * f);
        void add_tactic_after(tactic_factory * f);
        void add_plugin(plugin * p);
        virtual ::solver * operator()(ast_manager & m, params_ref const & p, bool proofs_enabled, bool models_enabled, bool unsat_core_enabled, symbol const & logic);
    };

};

#endif