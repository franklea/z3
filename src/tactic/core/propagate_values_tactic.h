/*++
Copyright (c) 2011 Microsoft Corporation

Module Name:

    propagate_values_tactic.h

Abstract:

    Propagate values using equalities of the form (= t v) where v is a value,
    and atoms t and (not t)

Author:

    Leonardo de Moura (leonardo) 2011-12-28.

Revision History:

--*/
#ifndef _PROPAGATE_VALUES_TACTIC_H_
#define _PROPAGATE_VALUES_TACTIC_H_

#include"params.h"
class ast_manager;
class tactic;

tactic * mk_propagate_values_tactic(ast_manager & m, params_ref const & p = params_ref());

/*
  ADD_TACTIC("propagate_values", "propagate constants.", "mk_propagate_values_tactic(m, p)")
*/

MK_SIMPLE_TACTIC_FACTORY(propagate_values_tactic_factory, mk_propagate_values_tactic(m, p));

#endif
