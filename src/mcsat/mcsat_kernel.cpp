/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    mcsat_kernel.cpp

Abstract:

    MCSAT kernel

Author:

    Leonardo de Moura (leonardo) 2012-11-01.

Revision History:

--*/
#include"mcsat_kernel.h"
#include"statistics.h"
#include"mcsat_expr_manager.h"
#include"mcsat_node_manager.h"
#include"mcsat_value_manager.h"
#include"mcsat_node_attribute.h"
#include"mcsat_plugin.h"

namespace mcsat {

    struct kernel::imp {
        
        class _initialization_context : public initialization_context {
            node_attribute_manager & m_attr_manager;
        public:
            _initialization_context(node_attribute_manager & m):m_attr_manager(m) {}
            virtual node_uint_attribute &   mk_uint_attribute() { return m_attr_manager.mk_uint_attribute(); }
            virtual node_double_attribute & mk_double_attribute() { return m_attr_manager.mk_double_attribute(); }
        };

        bool                      m_fresh;
        expr_manager              m_expr_manager;
        node_manager              m_node_manager;
        node_attribute_manager    m_attribute_manager;
        value_manager             m_value_manager;
        plugin_ref_vector         m_plugins;
        ptr_vector<trail>         m_trail_stack;
        unsigned_vector           m_plugin_qhead;
        _initialization_context   m_init_ctx;

        imp(ast_manager & m, bool proofs_enabled):
            m_expr_manager(m),
            m_attribute_manager(m_node_manager),
            m_init_ctx(m_attribute_manager) {
            m_fresh = true;
        }

        // Return true if the kernel is "fresh" and assertions were not added yet.
        bool is_fresh() const {
            return m_fresh;
        }

        void add_plugin(plugin * p) {
            SASSERT(is_fresh());
            p = p->clone();
            m_plugins.push_back(p);
            p->init(m_init_ctx);
        }
        
        void assert_expr(expr * f, proof * pr, expr_dependency * d) {
            m_fresh = false;
        }
        
        void push() {
        }

        void pop(unsigned num_scopes) {
        }

        lbool check_sat(unsigned num_assumptions, expr * const * assumptions) {
            return l_undef;
        }

        void collect_statistics(statistics & st) const {
        }

        void get_unsat_core(ptr_vector<expr> & r) {
        }

        void get_model(model_ref & m) {
        }

        proof * get_proof() {
            return 0;
        }

        std::string reason_unknown() const {
            return "unknown";
        }

        void set_cancel(bool f) {
        }

        void display(std::ostream & out) const {
        }
    };
    
    kernel::kernel(ast_manager & m, bool proofs_enabled) {
    }

    kernel::~kernel() {
    }

    void kernel::add_plugin(plugin * p) {
        m_imp->add_plugin(p);
    }
   
    void kernel::assert_expr(expr * f, proof * pr, expr_dependency * d) {
        m_imp->assert_expr(f, pr, d);
    }
        
    void kernel::push() {
        m_imp->push();
    }
     
    void kernel::pop(unsigned num_scopes) {
        m_imp->pop(num_scopes);
    }

    lbool kernel::check_sat(unsigned num_assumptions, expr * const * assumptions) {
        return m_imp->check_sat(num_assumptions, assumptions);
    }
    
    void kernel::collect_statistics(statistics & st) const {
        m_imp->collect_statistics(st);
    }
    
    void kernel::get_unsat_core(ptr_vector<expr> & r) {
        m_imp->get_unsat_core(r);
    }

    void kernel::get_model(model_ref & m) {
        m_imp->get_model(m);
    }
    
    proof * kernel::get_proof() {
        return m_imp->get_proof();
    }
    
    std::string kernel::reason_unknown() const {
        return m_imp->reason_unknown();
    }

    void kernel::set_cancel(bool f) {
        m_imp->set_cancel(f);
    }
    
    void kernel::display(std::ostream & out) const {
        m_imp->display(out);
    }

};