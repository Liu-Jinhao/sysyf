#include "IRBuilder.h"

// #define DEBUG

#ifdef DEBUG  // 用于调试信息,大家可以在编译过程中通过" -DDEBUG"来开启这一选项
#define DEBUG_OUTPUT std::cout << __LINE__ << std::endl;  // 输出行号的简单示例
#else
#define DEBUG_OUTPUT
#endif

#define CONST_INT(num) ConstantInt::get(num, module.get())
#define CONST_FLOAT(num) ConstantFloat::get(num, module.get())

#define Binary_op(op,type,lhs_val,rhs_val) \
    if((type)==INT32_T){\
        tmp_val=builder->create_i##op((lhs_val),(rhs_val));\
    }\
    else{\
        tmp_val=builder->create_f##op((lhs_val),(rhs_val));\
    }

#define LVal_to_RVal(lval) \
    if(lval->get_type()==INT32PTR_T || lval->get_type()==FLOATPTR_T){\
        lval=builder->create_load(lval);\
    }

#define create_idiv create_isdiv

#define Init_func_name "__cxx_global_var_init"

// You can define global variables and functions here
// to store state

// store temporary value
Value *tmp_val = nullptr;
std::vector<BasicBlock*> tmp_condbb_while;
std::vector<BasicBlock *> tmp_falsebb_while;
BasicBlock *tmp_truebb;
BasicBlock *tmp_falsebb;
bool break_or_continue;
std::vector<Value *> init_val;
Function *init_func = nullptr;

// types
Type *VOID_T;
Type *INT1_T;
Type *INT32_T;
Type *FLOAT_T;
Type *INT32PTR_T;
Type *FLOATPTR_T;

//从语法树类型到IR类型的映射
std::map<SyntaxTree::Type,Type *> type_map;//此处INT32_T还未初始化，不能在此添加映射关系

//将from_val转为t类型，主要考虑字面常量转型(字面常量无法load)
Value *static_cast_value(Value *from_val, Type *t, IRStmtBuilder *builder){
    Value *to_val;
    // auto literal_ptr=dynamic_cast<Constant *>(from_val);
    auto literal_int_ptr=dynamic_cast<ConstantInt *>(from_val);
    auto literal_float_ptr=dynamic_cast<ConstantFloat *>(from_val);
    if(from_val->get_type()==INT32_T && t==FLOAT_T){
        if(literal_int_ptr){
            to_val=ConstantFloat::get(literal_int_ptr->get_value(), builder->get_module());
        }
        else if(literal_float_ptr){
            to_val=from_val;
        }
        else{
            to_val=builder->create_sitofp(from_val,FLOAT_T);
        }
    }
    else if(from_val->get_type()==FLOAT_T && t==INT32_T){
        if(literal_int_ptr){
            to_val=from_val;
        }
        else if(literal_float_ptr){
            to_val=ConstantInt::get(static_cast<int>(literal_float_ptr->get_value()), builder->get_module());
        }
        else{
            to_val=builder->create_fptosi(from_val,INT32_T);
        }
    }
    else if(from_val->get_type()==INT1_T && t==INT32_T){//不允许i1类型的字面值
        to_val=builder->create_zext(from_val,INT32_T);
    }
    return to_val;
}

//将值val保存到地址addr。val可以是左值或右值，addr必须是左值(地址, 指针类型)
void store_to_address(Value *val, Value *addr, IRStmtBuilder *builder){
    LVal_to_RVal(val)
    if(addr->get_type()==INT32PTR_T && val->get_type()==FLOAT_T){
        val=static_cast_value(val,INT32_T,builder);
    }
    else if(addr->get_type()==FLOATPTR_T && val->get_type()==INT32_T){
        val=static_cast_value(val,FLOAT_T,builder);
    }
    builder->create_store(val,addr);
}

//假设已经通过了语法分析和语义检查，测试样例没有错误

void IRBuilder::visit(SyntaxTree::Assembly &node) {
    VOID_T = Type::get_void_type(module.get());
    INT1_T = Type::get_int1_type(module.get());
    INT32_T = Type::get_int32_type(module.get());
    FLOAT_T = Type::get_float_type(module.get());
    INT32PTR_T = Type::get_int32_ptr_type(module.get());
    FLOATPTR_T = Type::get_float_ptr_type(module.get());

    type_map[SyntaxTree::Type::VOID]=VOID_T;
    type_map[SyntaxTree::Type::BOOL]=INT1_T;
    type_map[SyntaxTree::Type::INT]=INT32_T;
    type_map[SyntaxTree::Type::FLOAT]=FLOAT_T;

    auto funTy = FunctionType::get(VOID_T, std::vector<Type *> {});
    init_func = Function::create(funTy, Init_func_name, builder->get_module());

    auto bb = BasicBlock::create(builder->get_module(), "entry", init_func);

    for (auto def : node.global_defs) {
        builder->set_insert_point(bb);//每次重置插入点为全局变量初始化函数，如果下面是函数定义则会被覆盖，没有问题
        def->accept(*this);
    }
    this->builder->set_insert_point(bb);
    this->builder->create_void_ret();
}

// You need to fill them

//C语言全局变量必须用常量初始化，但C++使用了一个内部函数来完成初始化
void IRBuilder::visit(SyntaxTree::InitVal &node) {
    if (node.isExp) {
        node.expr->accept(*this);
        LVal_to_RVal(tmp_val)
        init_val.push_back(tmp_val);//todo 假设是一维数组
    }
    else {
        for (auto element : node.elementList) {
            element->accept(*this);
            //todo 多维要记录tmp_val
        }
    }
}

void IRBuilder::visit(SyntaxTree::FuncDef &node) {
    std::vector<Type *> params{};
    for(auto param:node.param_list->params){
        params.push_back(type_map[param->param_type]);
    }
    auto funTy = FunctionType::get(type_map[node.ret_type], params);
    auto fun = Function::create(funTy, node.name, builder->get_module());
    scope.push(node.name,fun);

    auto bb = BasicBlock::create(builder->get_module(), "entry", fun);
    builder->set_insert_point(bb);
    
    scope.enter();

    node.param_list->accept(*this);

    if(node.name=="main"){
        if(init_func){
            builder->create_call(init_func, std::vector<Value *> {});
        }
    }

    for (auto stmt : node.body->body) {
        stmt->accept(*this);
        if(dynamic_cast<SyntaxTree::ReturnStmt*>(stmt.get())!=nullptr){//return后面的语句不再编译
            break;
        }
    }
    
    scope.exit();
}

void IRBuilder::visit(SyntaxTree::FuncFParamList &node) {
    auto func_args_iter=builder->get_module()->get_functions().back()->arg_begin();
    for(auto param:node.params){
        param->accept(*this);
        
        // 参数通过寄存器传递，要再分配空间并store
        auto paramAlloc=builder->create_alloca(type_map[param->param_type]);
        builder->create_store((*func_args_iter),paramAlloc);

        scope.push(param->name,paramAlloc);

        func_args_iter++;
    }
}

void IRBuilder::visit(SyntaxTree::FuncParam &node) {
    for(auto exp:node.array_index){//参数是数组才会执行这条循环
        // std::cout << "[";
        if (exp != nullptr){
            exp->accept(*this);
        }
        // std::cout << "]";
    }
}

void IRBuilder::visit(SyntaxTree::VarDef &node) {
    bool is_array = false;
    std::vector<int>array_dim_len{};
    for (auto length : node.array_length) {
        length->accept(*this);
        is_array = true;
        array_dim_len.push_back(dynamic_cast<ConstantInt *>(tmp_val)->get_value());//假设数组长度能在编译期确定(即是ConstantInt) 记录tmp_val(每一维的length) 以实现多维数组
    }

    Value *new_alloca;
    // std::cout << node.name << std::endl;

    if(scope.in_global()){
        //全局变量(常量)无论有没有初始化，一律全部初始化为0，再调用初始化函数，这是clang++的做法。clang对局部变量也是先全部分配好空间再逐个初始化的。
        if(is_array){
            // auto *arrayType = ArrayType::get(type_map[node.btype], dynamic_cast<SyntaxTree::Literal *>(node.array_length.front().get())->int_const);//todo front只实现了一维
            auto *arrayType = ArrayType::get(type_map[node.btype], array_dim_len.front());//todo front只实现了一维
            
            auto array_initializer = ConstantArray::get(arrayType, std::vector<Constant *>(arrayType->get_num_of_elements(),ConstantZero::get(type_map[node.btype], builder->get_module())));
            new_alloca = GlobalVariable::create(node.name, builder->get_module(), arrayType, false, array_initializer);//不用管node.is_constant，直接当成变量，因为经过语义检查器后常量跟变量没有任何区别，事实上clang++也是这样做的

            if (node.initializers) {//有初始化
                init_val.clear();//先清空
                node.initializers->accept(*this);

                //其实因为全局数组已经全部初始化为0，这里也可以省略这个循环
                //隐式初始化{}中的0，直到init_val.size() == arrayType->get_num_of_elements()
                while (init_val.size() < arrayType->get_num_of_elements()){
                    init_val.push_back(ConstantZero::get(type_map[node.btype], builder->get_module()));
                }

                for(int i=0;i<init_val.size();++i){//初始化
                    auto iGep = builder->create_gep(new_alloca, {CONST_INT(0), CONST_INT(i)});
                    store_to_address(init_val[i], iGep, builder);
                }
            }
        
        }
        else{//不是数组
            auto zero_initializer = ConstantZero::get(type_map[node.btype], builder->get_module());
            new_alloca = GlobalVariable::create(node.name, builder->get_module(), type_map[node.btype], false, zero_initializer);
            
            if (node.initializers) {//有初始化
                node.initializers->accept(*this);
                store_to_address(tmp_val, new_alloca, builder);
            }

        }
    }
    else{//不是全局变量
        if(is_array){
            // auto *arrayType = ArrayType::get(type_map[node.btype], dynamic_cast<SyntaxTree::Literal *>(node.array_length.front().get())->int_const);//假设数组长度能在编译期确定 todo front只实现了一维
            auto *arrayType = ArrayType::get(type_map[node.btype], array_dim_len.front());//todo front只实现了一维
            
            new_alloca = builder->create_alloca(arrayType);

            if (node.initializers) {//有初始化
                init_val.clear();//先清空
                node.initializers->accept(*this);

                //隐式初始化{}中的0，直到init_val.size() == arrayType->get_num_of_elements()
                while (init_val.size() < arrayType->get_num_of_elements()){
                    init_val.push_back(ConstantZero::get(type_map[node.btype], builder->get_module()));
                }

                for(int i=0;i<init_val.size();++i){//初始化
                    auto iGep = builder->create_gep(new_alloca, {CONST_INT(0), CONST_INT(i)});
                    store_to_address(init_val[i], iGep, builder);
                }
            }

        }
        else{//不是数组
            new_alloca = builder->create_alloca(type_map[node.btype]);
            
            if (node.initializers) {//有初始化
                node.initializers->accept(*this);
                store_to_address(tmp_val, new_alloca, builder);
            }

        }
    }

    scope.push(node.name,new_alloca);//符号表中保存的全是指针类型

}

//返回的tmp_val是左值(地址)，用到值时要转为右值(用load指令)
void IRBuilder::visit(SyntaxTree::LVal &node) {
    auto lval=scope.find(node.name,false);//符号表中保存的全是指针类型
    if(lval){
        if(lval->get_type()->is_pointer_type()==false){
            std::cout << "LVal is not address(pointer type)" << std::endl;
        }

        if(!node.array_index.empty()){
            for (auto index : node.array_index) {
                index->accept(*this);
                //todo ...=tmp_val 用一个vector记录多维数组的下标，现在只实现了一维
            }
            LVal_to_RVal(tmp_val)
            tmp_val=builder->create_gep(lval,{CONST_INT(0), tmp_val});
        }
        else{
            tmp_val=lval;
        }

    }
    else{
        std::cout << "LVal not found" << std::endl;
    }
    
}

void IRBuilder::visit(SyntaxTree::AssignStmt &node) {
    //赋值表达式先算右边
    node.value->accept(*this);
    auto val=tmp_val;
    node.target->accept(*this);
    auto addr=tmp_val;

    store_to_address(val,addr,builder);
}

void IRBuilder::visit(SyntaxTree::Literal &node) {
    if(node.literal_type == SyntaxTree::Type::INT){
        tmp_val=CONST_INT(node.int_const);
    }
    else{
        tmp_val=CONST_FLOAT(node.float_const);
    }
}

void IRBuilder::visit(SyntaxTree::ReturnStmt &node) {
    if(node.ret!=nullptr){
        node.ret->accept(*this);
        LVal_to_RVal(tmp_val)
        //返回值不是字面常量，不用考虑字面转型
        if(builder->get_module()->get_functions().back()->get_function_type()->get_return_type()==INT32_T && tmp_val->get_type()!=INT32_T){
            tmp_val=builder->create_fptosi(tmp_val,INT32_T);
        }
        else if(builder->get_module()->get_functions().back()->get_function_type()->get_return_type()!=INT32_T && tmp_val->get_type()==INT32_T){
            tmp_val=builder->create_sitofp(tmp_val,FLOAT_T);
        }
        this->builder->create_ret(tmp_val);
    }
    else{
        this->builder->create_void_ret();
    }
}

void IRBuilder::visit(SyntaxTree::BlockStmt &node) {
    this->scope.enter();
    for(auto stmt : node.body){
        stmt->accept(*this);
        if(break_or_continue==true){
            break;
        }
    }
    this->scope.exit();
}

void IRBuilder::visit(SyntaxTree::EmptyStmt &node) {}

void IRBuilder::visit(SyntaxTree::ExprStmt &node) {
    node.exp->accept(*this);
}

void IRBuilder::visit(SyntaxTree::UnaryCondExpr &node) {}

void IRBuilder::visit(SyntaxTree::BinaryCondExpr &node) {
    Value *lval, *rval, *icmp;
    if (node.op == SyntaxTree::BinaryCondOp::LAND){
        node.lhs->accept(*this);
        lval = tmp_val;
        auto falseBB = tmp_falsebb;
        auto trueBB = BasicBlock::create(this->builder->get_module(), "trueBB_lhs", this->builder->get_module()->get_functions().back());
        if(lval->get_type()==INT1_T){
            this->builder->create_cond_br(lval, trueBB, falseBB);
        }
        else{
            if(lval->get_type()==INT32_T)
                icmp = this->builder->create_icmp_ne(lval, CONST_INT(0));
            else
                icmp = this->builder->create_fcmp_ne(lval, CONST_FLOAT(0));
            this->builder->create_cond_br(icmp, trueBB, falseBB);
        }
        this->builder->set_insert_point(trueBB);
        node.rhs->accept(*this);
    }
    else if(node.op==SyntaxTree::BinaryCondOp::LOR){
        node.lhs->accept(*this);
        lval = tmp_val;
        LVal_to_RVal(lval);
        auto falseBB = BasicBlock::create(this->builder->get_module(), "trueBB_lhs", this->builder->get_module()->get_functions().back());
        auto trueBB = tmp_truebb;
        if (lval->get_type() == INT1_T){
            this->builder->create_cond_br(lval, trueBB, falseBB);
        }
        else{
            if(lval->get_type()==INT32_T)
                icmp = this->builder->create_icmp_ne(lval, CONST_INT(0));
            else
                icmp = this->builder->create_fcmp_ne(lval, CONST_FLOAT(0));
            this->builder->create_cond_br(icmp, trueBB, falseBB);
        }
        this->builder->set_insert_point(falseBB);
        node.rhs->accept(*this);
    }
    else{
        node.lhs->accept(*this);
        lval = tmp_val;
        LVal_to_RVal(lval);
        node.rhs->accept(*this);
        rval = tmp_val;
        LVal_to_RVal(rval);
        if (lval->get_type() != INT1_T){
            if (rval->get_type()!=INT1_T){
                if(lval->get_type()==FLOAT_T&&rval->get_type()==INT32_T){
                    static_cast_value(rval, FLOAT_T, this->builder);
                }
                else if(lval->get_type()==INT32_T&&rval->get_type()==FLOAT_T){
                    static_cast_value(lval, FLOAT_T, this->builder);
                }
            }
            else{
                if(lval->get_type()==INT32_T){
                    static_cast_value(rval, INT32_T, this->builder);
                }
                else{
                    static_cast_value(rval, FLOAT_T, this->builder);
                }
            }
        }
        else{
            if(rval->get_type()!=INT1_T){
                if(rval->get_type()==INT32_T){
                    static_cast_value(lval, INT32_T, this->builder);
                }
                else{
                    static_cast_value(lval, FLOAT_T, this->builder);
                }
            }
            else{
                static_cast_value(lval, INT32_T, this->builder);
                static_cast_value(rval, INT32_T, this->builder);
            }
        }
        if(lval->get_type()==INT32_T){
            if(node.op==SyntaxTree::BinaryCondOp::LT){
                tmp_val = this->builder->create_icmp_lt(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::LTE){
                tmp_val = this->builder->create_icmp_le(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::GT){
                tmp_val = this->builder->create_icmp_gt(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::GTE){
                tmp_val = this->builder->create_icmp_ge(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::EQ){
                tmp_val = this->builder->create_icmp_eq(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::NEQ){
                tmp_val = this->builder->create_icmp_ne(lval, rval);
            }
        }
        else{
            if(node.op==SyntaxTree::BinaryCondOp::LT){
                tmp_val = this->builder->create_fcmp_lt(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::LTE){
                tmp_val = this->builder->create_fcmp_le(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::GT){
                tmp_val = this->builder->create_fcmp_gt(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::GTE){
                tmp_val = this->builder->create_fcmp_ge(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::EQ){
                tmp_val = this->builder->create_fcmp_eq(lval, rval);
            }
            else if(node.op==SyntaxTree::BinaryCondOp::NEQ){
                tmp_val = this->builder->create_fcmp_ne(lval, rval);
            }
        }
    }
}

void IRBuilder::visit(SyntaxTree::BinaryExpr &node) {
    node.lhs->accept(*this);
    auto lhs_val = tmp_val;
    LVal_to_RVal(lhs_val)
    auto lhs_type = lhs_val->get_type();
    node.rhs->accept(*this);
    auto rhs_val = tmp_val;
    LVal_to_RVal(rhs_val)
    auto rhs_type = rhs_val->get_type();

    auto expr_type=FLOAT_T;
    if(lhs_type==FLOAT_T && !(rhs_type==FLOAT_T)){
        rhs_val=static_cast_value(rhs_val,FLOAT_T,builder);
    }
    else if(!(lhs_type==FLOAT_T) && rhs_type==FLOAT_T){
        lhs_val=static_cast_value(lhs_val,FLOAT_T,builder);
    }
    else if(lhs_type==INT32_T && rhs_type==INT32_T){
        expr_type=INT32_T;
    }
    
  
    switch(node.op){
        case SyntaxTree::BinOp::PLUS:
            Binary_op(add,expr_type,lhs_val,rhs_val)
            break;
        case SyntaxTree::BinOp::MINUS:
            Binary_op(sub,expr_type,lhs_val,rhs_val)
            break;
        case SyntaxTree::BinOp::MULTIPLY:
            Binary_op(mul,expr_type,lhs_val,rhs_val)
            break;
        case SyntaxTree::BinOp::DIVIDE:
            Binary_op(div,expr_type,lhs_val,rhs_val)
            break;
        case SyntaxTree::BinOp::MODULO:
            tmp_val=builder->create_isrem(lhs_val,rhs_val);//不检查取模类型
            break;
    }

}

void IRBuilder::visit(SyntaxTree::UnaryExpr &node) {
    node.rhs->accept(*this);
    LVal_to_RVal(tmp_val)

    if(node.op==SyntaxTree::UnaryOp::MINUS){
        if(tmp_val->get_type()==INT32_T){
            tmp_val=builder->create_isub(CONST_INT(0),tmp_val);
        }
        else{
            tmp_val=builder->create_fsub(CONST_FLOAT(0),tmp_val);
        }
    }
}

void IRBuilder::visit(SyntaxTree::FuncCallStmt &node) {
    auto func=scope.find(node.name,true);
    if(func){
        std::vector<Value *> args{};
        auto func_args_iter=dynamic_cast<Function *>(func)->arg_begin();
        for(auto exp:node.params){
            exp->accept(*this);
            LVal_to_RVal(tmp_val)
            //函数调用参数转型
            if(tmp_val->get_type()==INT32_T && (*func_args_iter)->get_type()==FLOAT_T){
                tmp_val=static_cast_value(tmp_val,FLOAT_T,builder);
            }
            else if(tmp_val->get_type()==FLOAT_T && (*func_args_iter)->get_type()==INT32_T){
                tmp_val=static_cast_value(tmp_val,INT32_T,builder);
            }
            args.push_back(tmp_val);
            func_args_iter++;
        }
        tmp_val=builder->create_call(func,args);//void函数也可以
    }
}

void IRBuilder::visit(SyntaxTree::IfStmt &node) {
    auto trueBB = BasicBlock::create(this->builder->get_module(), "trueBB_if", this->builder->get_module()->get_functions().back());
    auto falseBB = BasicBlock::create(this->builder->get_module(), "falseBB_if", this->builder->get_module()->get_functions().back());
    tmp_truebb = trueBB;
    tmp_falsebb = falseBB;
    node.cond_exp->accept(*this);
    this->builder->create_cond_br(tmp_val, trueBB, falseBB);
    this->builder->set_insert_point(trueBB);
    node.if_statement->accept(*this);
    this->builder->set_insert_point(falseBB);
    if(node.else_statement!=nullptr){
        node.else_statement->accept(*this);
    }
}

void IRBuilder::visit(SyntaxTree::WhileStmt &node) {
    auto condBB=BasicBlock::create(this->builder->get_module(),"condBB_while",this->builder->get_module()->get_functions().back());
    auto trueBB=BasicBlock::create(this->builder->get_module(),"trueBB_while",this->builder->get_module()->get_functions().back());
    auto falseBB=BasicBlock::create(this->builder->get_module(),"falsedBB_while",this->builder->get_module()->get_functions().back());
    this->builder->create_br(condBB);
    this->builder->set_insert_point(condBB);
    tmp_truebb = trueBB;
    tmp_falsebb = falseBB;
    node.cond_exp->accept(*this);
    this->builder->create_cond_br(tmp_val, trueBB, falseBB);
    this->builder->set_insert_point(trueBB);
    tmp_condbb_while.push_back(condBB);
    tmp_falsebb_while.push_back(falseBB);
    break_or_continue = false;
    node.statement->accept(*this);
    this->builder->create_br(condBB);
    this->builder->set_insert_point(falseBB);
    tmp_condbb_while.pop_back();
    tmp_falsebb_while.pop_back();
    break_or_continue = false;
}

void IRBuilder::visit(SyntaxTree::BreakStmt &node) {
    this->builder->create_br(tmp_falsebb_while.back());
    break_or_continue = true;
}

void IRBuilder::visit(SyntaxTree::ContinueStmt &node) {
    this->builder->create_br(tmp_condbb_while.back());
    break_or_continue = true;
}

