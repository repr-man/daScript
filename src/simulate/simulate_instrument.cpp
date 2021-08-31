#include "daScript/misc/platform.h"

#include "daScript/simulate/simulate.h"
#include "daScript/simulate/simulate_nodes.h"

namespace das {

#if DAS_DEBUGGER

    struct SimInstVisitor : SimVisitor {
        bool isCorrectFileAndLine ( const LineInfo & info ) {
            if ( lineNumber>=info.line && lineNumber<=info.last_line ) {
                if ( info.fileInfo && info.fileInfo->name==fileName ) {
                    return true;
                }
            }
            return false;
        }
        SimNode * instrumentNode ( SimNode * expr ) {
            if ( !expr->rtti_node_isInstrument() ) {
                return context->code->makeNode<SimNodeDebug_Instrument>(expr->debugInfo, expr);
            } else {
                return expr;
            }
        }
        SimNode * clearNode ( SimNode * expr ) {
            if ( expr->rtti_node_isInstrument() ) {
                auto si = (SimNodeDebug_Instrument *) expr;
                return si->subexpr;
            } else {
                return expr;
            }
        }
        virtual SimNode * visit ( SimNode * node ) override {
            if ( node->rtti_node_isBlock() ) {
                SimNode_Block * blk = (SimNode_Block *) node;
                for ( uint32_t i=0; i!=blk->total; ++i ) {
                    auto & expr = blk->list[i];
                    if ( anyLine || isCorrectFileAndLine(expr->debugInfo) ) {
                        expr = isInstrumenting ? instrumentNode(expr) : clearNode(expr);
                    }
                }
                for ( uint32_t i=0; i!=blk->totalFinal; ++i ) {
                    auto & expr = blk->finalList[i];
                    if ( anyLine || isCorrectFileAndLine(expr->debugInfo) ) {
                        expr = isInstrumenting ? instrumentNode(expr) : clearNode(expr);
                    }
                }
            }
            return node;
        }
        Context * context = nullptr;
        const char * fileName = nullptr;
        uint32_t lineNumber = 0;
        bool isInstrumenting = true;
        bool anyLine = false;
    };

    void Context::instrumentContextNode ( const char * fileName, int32_t lineNumber, bool isInstrumenting ) {
        SimInstVisitor instrument;
        instrument.context = this;
        instrument.fileName = fileName;
        instrument.lineNumber = uint32_t(lineNumber);
        instrument.isInstrumenting = isInstrumenting;
        runVisitor(&instrument);
    }

    void Context::clearInstruments() {
        SimInstVisitor instrument;
        instrument.context = this;
        instrument.isInstrumenting = false;
        instrument.anyLine = true;
        runVisitor(&instrument);
        instrumentFunction(-1, false);
    }

    void Context::instrumentFunction ( int index, bool isInstrumenting ) {
        auto instFn = [&](SimFunction * fun, int32_t fnIndex) {
            if ( !fun->code ) return;
            if ( isInstrumenting ) {
                if ( !fun->code->rtti_node_isInstrumentFunction() ) {
                    fun->code = code->makeNode<SimNodeDebug_InstrumentFunction>(fun->code->debugInfo, fun, fnIndex, fun->code);
                }
            } else {
                if ( fun->code->rtti_node_isInstrumentFunction() ) {
                    auto inode = (SimNodeDebug_InstrumentFunction *) fun->code;
                    fun->code = inode->subexpr;
                }
            }
        };
        if ( index==-1 ) {
            for ( int fni=0; fni!=totalFunctions; ++fni ) {
                instFn(&functions[fni], fni);
            }
        } else {
            instFn(&functions[index], index);
        }
    }
#else
    void Context::instrumentFunction ( int, bool ) {}
    void Context::instrumentContextNode ( const char *, int32_t, bool ) {}
    void Context::clearInstruments() {}
#endif

}
