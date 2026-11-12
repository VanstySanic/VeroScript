// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

namespace VeroScript::CppScriptFixture
{
	inline constexpr const char* Extension = R"vs(
import "compiler.vs";
import "string.vs";
using callback=fn(std::compiler::context,std::compiler::output)->void;
int calls=0;
callback make(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 calls+=1;
 if(ctx.phase()==std::compiler::phase::raw_source || ctx.phase()==std::compiler::phase::active_source){
  string word=ctx.phase()==std::compiler::phase::raw_source?"MARK":"ACTIVE";
  int p=std::strings::find(ctx.source().text(),word);
  if(p>=0){std::compiler::source_range r=std::compiler::source_range();r.source_id=ctx.module_id();r.begin=p;r.end=p+len(word);
   out.edit_source(r,ctx.phase()==std::compiler::phase::raw_source?"ACTIVE":"1");}
 }else if(ctx.phase()==std::compiler::phase::types){
  out.add_declaration(ctx.top_scope(),"struct Generated{int value=2;};");
 }else if(ctx.phase()==std::compiler::phase::bodies){
  if(calls!=5)out.report_diagnostic("lost script extension state",0);
  auto amount=ctx.configuration("amount");auto dep=ctx.read_dependency("settings.txt");
  if(!amount.exists() || !dep.exists())out.report_diagnostic("missing script extension input",0);
  auto ds=ctx.declarations();for(int i=0;i<len(ds);++i)if(ds[i].module_id()==ctx.module_id() && ds[i].name()=="Pending")
   out.complete_body(ds[i].id(),"{return "+amount.value()+"+"+dep.content()+";}");
 }
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
 std::compiler::processor_definition d=std::compiler::processor_definition();d.id="cpp.script";d.version="1";
 d.phases=array<std::compiler::phase>{std::compiler::phase::raw_source,std::compiler::phase::active_source,std::compiler::phase::types,std::compiler::phase::members,std::compiler::phase::bodies};
 d.operations=array<std::compiler::operation>{std::compiler::operation::edit_source,std::compiler::operation::add_declaration,std::compiler::operation::complete_body};
 d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,make);
}
)vs";
	inline constexpr const char* Source = "int kept=MARK;int Pending();int value(){Generated g=Generated();return Pending()+kept+g.value;}void main(){print(value());}";
}
