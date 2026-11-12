// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptAst.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptCodegen.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptVm.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>

using namespace VeroScript;
namespace
{
	int Failures = 0;
	void Expect(bool Yes, const char* Message)
	{
		if (!Yes)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}
	struct FGetter : IAnnotationProcessor
	{
		void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
		{
			O.SubmitMethod(C.Owner->Id, EMemberAccess::Public,
			    C.Declaration.TypeName + " get_" + C.Declaration.Name + "() const { return " +
			        C.Declaration.Name + "; }");
		}
	};
	struct FRpc : IAnnotationProcessor
	{
		void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
		{
			if (!C.Declaration.bHasBody)
			{
				O.CompleteMethodBody(C.Declaration.Id, "{ if (Test_Validation()) Test_Implementation(); }");
			}

			for (const auto& Name : {std::string("Test_Implementation"), std::string("Test_Validation")})
			{
				const FAnnotationDeclarationView* Found = nullptr;
				for (const auto& D : C.Declarations)
				{
					if (D.OwnerId == C.Owner->Id && D.Name == Name)
					{
						Found = &D;
					}
				}

				const bool Validation = Name == "Test_Validation";
				const std::string Body = Validation ? "{ return false; }" : "{}";
				if (!Found)
				{
					O.SubmitMethod(C.Owner->Id, EMemberAccess::Public,
					    (Validation ? "virtual bool " : "virtual void ") + Name + "() " + Body);
				}

				else if (!Found->bHasBody)
				{
					O.CompleteMethodBody(Found->Id, Body);
				}
			}
		}
	};
	FCompileOptions Options()
	{
		auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
		std::string Error;
		Expect(Registry->Register(
		           {"@Getter", "tests.getter", "1", "", AnnotationTargetMask(EAnnotationTarget::Field), 1, 0,
		               []
		{
			return std::make_unique<FGetter>();
		}},
		           Error),
		    "register getter");
		Expect(Registry->Register({"UFUNCTION", "tests.rpc", "1", "validation=false;implementation=empty",
		                              AnnotationTargetMask(EAnnotationTarget::Method), 3, 0,
		                              []
		{
			return std::make_unique<FRpc>();
		}},
		           Error),
		    "register rpc");
		Registry->Freeze();
		FCompileOptions O = VeroScript::Test::MakeCompileOptions();
		O.AnnotationProcessing.Registry = Registry;
		return O;
	}
	bool Run(FCompileResult& R, std::string Expected)
	{
		if (!R.bSuccess)
		{
			std::cerr << R.Error.Location.FileLabel << ":" << R.Error.Location.Line << ":"
			          << R.Error.Location.Column << ": " << R.Error.Message << '\n';
			return false;
		}
		std::ostringstream Out, Err;
		std::istringstream In;
		VeroScriptVm Vm(Out, In, Err);
		VeroScript::Test::Configure(Vm);
		if (!Vm.Execute(*R.Module, ERunMode::Script))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return false;
		}
		return Out.str() == Expected;
	}
} // namespace
int main()
{
	auto O = Options();
	{
		FHostBindingRegistry Bindings;
		std::string Error;
		Expect(Bindings.RegisterAnnotation("@Getter", "", Error, "", {}, AnnotationAllTargets, false), "register getter name policy");
		auto Configured = O;
		Configured.Bindings = &Bindings;
		auto Generated = VeroScriptCompile::CompileSource("struct Item { @gEtTeR int value=19; };void main(){Item i=Item();print(i.get_value());}", "getter-name-policy.vs", Configured);
		Expect(Run(Generated, "19\n"), "annotation convenience processors share the registered name policy");
	}
	auto Getter = VeroScriptCompile::CompileSource(
	    "struct Item { @Getter int value = 7; }; void main(){ Item x=Item(); "
	    "print(x.get_value()); }",
	    "getter.vs", O);
	Expect(Run(Getter, "7\n"), "bare annotation generates a callable method");
	auto Generic = VeroScriptCompile::CompileSource(
	    "struct Box<T> { @Getter T value; Box(T input){ value=input; } }; void "
	    "main(){ Box<int> x(9); "
	    "print(x.get_value()); Box<string> s(\"yes\"); print(s.get_value()); }",
	    "generic.vs", O);
	Expect(Run(Generic, "9\nyes\n"), "generated methods instantiate for multiple generic arguments");
	auto Virtual =
	    VeroScriptCompile::CompileSource("class Base { public: UFUNCTION(Replicated, WithValidation) virtual "
	                                     "void Test(); }; class Child : Base { "
	                                     "public: bool Test_Validation() override { return true; } void "
	                                     "Test_Implementation() override { print(11); } "
	                                     "}; void main(){ Child c=Child(); Base b=c; b.Test(); }",
	        "virtual.vs", O);
	Expect(Run(Virtual, "11\n"), "generated virtual helpers use ordinary derived dispatch");
	O.SourceOverlays = {{"decl.vs", "export class A { public: UFUNCTION(Replicated) void Test(); virtual "
	                                "bool Test_Validation(); "
	                                "virtual void Test_Implementation(); }; export int Sum(int a, int b);"},
	    {"impl.vs", "import \"decl.vs\"; int helper(){return 40;} bool "
	                "A::Test_Validation(){return true;} void "
	                "A::Test_Implementation(){print(helper());} int ::Sum(int "
	                "x,int y){return x+y;}"}};
	auto Cross = VeroScriptCompile::CompileSource(
	    "import \"decl.vs\"; import \"impl.vs\"; void main(){ A x=A(); x.Test(); "
	    "print(Sum(2,3)); }",
	    "main.vs", O);
	Expect(Run(Cross, "40\n5\n"), "cross-file bodies precede default generation "
	                              "and bind implementation parameter names");
	O.SourceOverlays = {{"decl.vs", "export struct Box<T>{ T value; Box(T "
	                                "input){value=input;} T Get() const; }; namespace API { "
	                                "template<T> export T Echo(T value); }"},
	    {"impl.vs", "import \"decl.vs\"; T Box<T>::Get() const "
	                "{return value;} template<T> T API::Echo(T "
	                "input){return input;}"}};
	auto CrossGeneric = VeroScriptCompile::CompileSource(
	    "import \"decl.vs\"; import \"impl.vs\"; void main(){ Box<int> x(13); "
	    "print(x.Get()); print(API::Echo<string>(\"ok\")); }",
	    "main.vs", O);
	Expect(Run(CrossGeneric, "13\nok\n"),
	    "generic method and generic free function supplemental definitions");
	{
		auto FunctionReturn = VeroScriptCompile::CompileSource(
		    "struct Factory{fn(int)->int Make();};"
		    "fn(int)->int Factory::Make(){return [](int n)->int => n+1;}"
		    "fn()->fn(int)->int Nested();"
		    "fn()->fn(int)->int ::Nested(){return []()->fn(int)->int => [](int n)->int => n+2;}"
		    "void main(){Factory f=Factory();print(f.Make()(4));print(Nested()()(4));}",
		    "function-return-definition.vs");
		Expect(Run(FunctionReturn, "5\n6\n"),
		    "qualified definitions distinguish function return type parentheses from the parameter list");
		FCompileOptions FunctionOptions = VeroScript::Test::MakeCompileOptions();
		FunctionOptions.SourceOverlays = {
		    {"function-decl.vs", "export struct Factory<T>{T value;fn(T)->T Make();};"
		                         "namespace API{export fn(int)->int Make();}"},
		    {"function-impl.vs", "import \"function-decl.vs\";"
		                         "fn(T)->T Factory<T>::Make(){return [](T n)->T => n;}"
		                         "fn(int)->int API::Make(){return [](int n)->int => n+3;}"}};
		auto CrossFunctionReturn = VeroScriptCompile::CompileSource(
		    "import \"function-decl.vs\";import \"function-impl.vs\";"
		    "void main(){Factory<int> f=Factory<int>();print(f.Make()(7));print(API::Make()(4));}",
		    "function-main.vs", FunctionOptions);
		Expect(Run(CrossFunctionReturn, "7\n7\n"),
		    "cross-file generic methods and functions may return callable types");
		auto Conversion = VeroScriptCompile::CompileSource(
		    "namespace N{using Number=int;}struct Converter{N::Number operator N::Number();};"
		    "N::Number Converter::operator N::Number(){return 7;}"
		    "void main(){Converter c=Converter();int n=c;print(n);print(cast<N::Number>(c));}",
		    "conversion-definition.vs");
		Expect(Run(Conversion, "7\n7\n"),
		    "qualified conversion definitions bind the owner before operator, not the target namespace");
		auto GenericConversion = VeroScriptCompile::CompileSource(
		    "struct Converter<T>{T value;Converter(T n){value=n;}T operator T(){return value;}};"
		    "void main(){Converter<int> c(9);int n=c;print(n);print(cast<int>(c));}",
		    "generic-conversion.vs");
		Expect(Run(GenericConversion, "9\n9\n"),
		    "conversion target lookup substitutes generic owner parameters");
		auto AmbiguousConversion = VeroScriptCompile::CompileSource(
		    "using Number=int;struct Converter{Number operator Number(){return 1;}"
		    "int operator int(){return 2;}};void main(){Converter c=Converter();int n=c;}",
		    "ambiguous-conversion.vs");
		Expect(!AmbiguousConversion.bSuccess && AmbiguousConversion.Error.Message.find("ambiguous") != std::string::npos,
		    "equivalent conversion target aliases cannot silently select different methods");
		auto PrivateConversion = VeroScriptCompile::CompileSource(
		    "using Number=int;class Converter{private:Number operator Number(){return 1;}};"
		    "void main(){Converter c=Converter();int n=c;}",
		    "private-conversion.vs");
		Expect(!PrivateConversion.bSuccess && PrivateConversion.Error.Message.find("private") != std::string::npos,
		    "canonical conversion lookup still checks member access");
		auto ConversionLibrary = VeroScriptCompile::CompileSource(
		    "namespace N{export using Number=int;export using Other=int;}"
		    "export struct ClosedConverter{N::Number operator N::Number(){return 7;}};"
		    "export class VirtualBase{public:virtual N::Number operator N::Number()=0;};"
		    "export class VirtualChild:public VirtualBase{public:N::Other operator N::Other() override{return 2;}};"
		    "export struct GenericConverter<T>{T value;GenericConverter(T n){value=n;}"
		    "T operator T(){return value;}};",
		    "conversion-library.vs");
		FPackageBuildOptions ConversionBuild;
		ConversionBuild.Kind = EPackageKind::Library;
		ConversionBuild.PackageName = "Conversions";
		ConversionBuild.bStripDebugInfo = true;
		std::vector<std::uint8_t> ConversionBytes;
		std::string ConversionError;
		const bool PackagedConversions = ConversionLibrary.bSuccess &&
		    BuildPackage(*ConversionLibrary.Module, ConversionBuild, ConversionBytes, ConversionError);
		Expect(PackagedConversions, "package aliased and generic conversion operators");
		if (PackagedConversions)
		{
			const auto Dir = std::filesystem::temp_directory_path() /
			    ("veroscript-conversions-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Dir);
			const auto File = Dir / "Conversions.vslib";
			{
				std::ofstream Out(File, std::ios::binary);
				Out.write(reinterpret_cast<const char*>(ConversionBytes.data()), ConversionBytes.size());
			}
			auto PackageConversions = VeroScriptCompile::CompileSource(
			    "import \"Conversions.vslib\";void main(){ClosedConverter c=ClosedConverter();"
			    "int a=c;GenericConverter<int> g(9);int b=g;print(a);print(b);"
			    "VirtualChild child=VirtualChild();VirtualBase base=child;int v=base;print(v);"
			    "type<VirtualBase> info=type<VirtualBase>();for(int i=0;i<info.method_count();++i){"
			    "method_info<VirtualBase> method(info.qualified_name,i);if(method.name()==\"operator conv N.Number\")"
			    "print(method.invoke<int>(child));}}",
			    (Dir / "consumer.vs").generic_string());
			Expect(Run(PackageConversions, "7\n9\n2\n2\n"),
			    "package consumers retain conversion method identity and instantiate target parameters");
			std::filesystem::remove(File);
			std::filesystem::remove(Dir);
		}
	}
	{
		const std::string MutableConversion =
		    "struct C{int value=0;int operator int(){value=9;return value;}};";
		for (const auto& Body : {
		         "void main(){const C c=C();int n=c;}",
		         "void main(){const C c=C();print(cast<int>(c));}",
		         "int Read(const C& c){return c;}void main(){}",
		         "void Take(int n){}void main(){const C c=C();Take(c);}",
		         "struct Holder{int value;Holder(const C& c):value(c){}};void main(){}"})
		{
			auto Rejected = VeroScriptCompile::CompileSource(MutableConversion + Body, "const-conversion.vs");
			Expect(!Rejected.bSuccess, "const conversion receiver cannot call a mutating conversion method");
		}
		auto ConstOverload = VeroScriptCompile::CompileSource(
		    "struct C{int operator int(){return 1;}int operator int() const{return 2;}};"
		    "void main(){C m=C();const C c=C();int a=m;int b=c;print(a);print(b);print(cast<int>(c));}",
		    "const-conversion-overloads.vs");
		Expect(Run(ConstOverload, "1\n2\n2\n"), "conversion overload selection preserves receiver constness");
		auto MutableCopy = VeroScriptCompile::CompileSource(MutableConversion +
		        "void main(){const C c=C();C copy=c;int n=copy;print(n);print(c.value);}",
		    "conversion-copy.vs");
		Expect(Run(MutableCopy, "9\n0\n"), "copying a const value still produces mutable independent storage");
		auto AliasedDefinition = VeroScriptCompile::CompileSource(
		    "using Number=int;struct C{Number operator Number();};int C::operator int(){return 3;}"
		    "void main(){C c=C();int n=c;print(n);}",
		    "conversion-alias-definition.vs");
		Expect(Run(AliasedDefinition, "3\n"), "separated conversion definitions match equivalent target spellings");
		for (const auto& Types : {std::pair<std::string, std::string>{"long", "int64"},
		         std::pair<std::string, std::string>{"unsigned long", "uint64"}})
		{
			const auto& [Written, Equivalent] = Types;
			for (const auto& Declaration : {"{return 1;}", "=0;"})
			{
				auto FixedWidth = VeroScriptCompile::CompileSource(
				    "class Base{public:virtual " + Written + " operator " + Written + "()" + Declaration +
				        "};class Derived:public Base{public:" + Equivalent + " operator " + Equivalent +
				        "() override{return 2;}};void main(){Derived d=Derived();Base b=d;" +
				        Written + " n=b;print(n);}",
				    "fixed-width-virtual-conversion.vs");
				Expect(Run(FixedWidth, "2\n"), "virtual and pure conversion slots share fixed-width alias identity");
			}
			auto FixedDefinition = VeroScriptCompile::CompileSource(
			    "struct C{" + Written + " operator " + Written + "();};" + Equivalent +
			        " C::operator " + Equivalent + "(){return 3;}void main(){C c=C();" + Written +
			        " n=c;print(n);}",
			    "fixed-width-conversion-definition.vs");
			Expect(Run(FixedDefinition, "3\n"), "separated conversions match fixed-width alias targets");
			for (const auto* OwnerKind : {"class", "interface"})
			{
				auto Reflected = VeroScriptCompile::CompileSource(std::string(OwnerKind) +
				        " Base{public:virtual " + Written + " operator " + Written + "()=0;};"
				                                                                     "class Derived:public Base{public:" +
				        Equivalent + " operator " + Equivalent +
				        "() override{return 2;}};void main(){Derived d=Derived();type<Base> info=type<Base>();"
				        "for(int i=0;i<info.method_count();++i){method_info<Base> method(info.qualified_name,i);"
				        "if(method.name()==\"operator conv " +
				        Written + "\"){print(method.invoke<" + Written +
				        ">(d));print(method.invoke<" + Equivalent + ">(d));}}}",
				    "fixed-width-reflection-conversion.vs");
				Expect(Run(Reflected, "2\n2\n"), "reflected class and interface conversions retain aliases and derived dispatch");
				if (Reflected.bSuccess)
				{
					std::vector<std::uint8_t> Bytes;
					std::string Error;
					Expect(VeroScriptBytecodeIO::SerializeBytecode(*Reflected.Module, 11, Bytes, Error),
					    "serialize fixed-width conversion bytecode");
					FCompileResult Loaded;
					Loaded.Module = VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 11, Error);
					Loaded.bSuccess = bool(Loaded.Module);
					Loaded.Error.Message = Error;
					Expect(Run(Loaded, "2\n2\n"), "loaded bytecode retains fixed-width reflected conversion dispatch");
				}
			}
			auto GenericFixedWidth = VeroScriptCompile::CompileSource(
			    "class Base<T>{public:virtual T operator T(){return 1;}};"
			    "class Derived:public Base<" +
			        Written + ">{public:" + Equivalent + " operator " + Equivalent +
			        "(){return 2;}};void main(){Derived d=Derived();Base<" + Written + "> b=d;" +
			        Written + " n=b;print(n);}",
			    "generic-fixed-width-conversion.vs");
			Expect(Run(GenericFixedWidth, "2\n"), "generic conversion aliases preserve implicit virtual overrides");
			auto ScalarReflection = VeroScriptCompile::CompileSource(
			    "class Value{public:" + Written + " number=1;" + Written + "& Get(){return number;}"
			                                                               "const " +
			        Written + "& Read() const{return number;}void Bump(" + Written + "& n){n+=cast<" + Written + ">(1);}};"
			                                                                                                     "void main(){Value value=Value();type<Value> info=type<Value>();"
			                                                                                                     "field_info<Value> field(info.qualified_name,0);print(field.get<" +
			        Equivalent + ">(value));"
			                     "field.set<" +
			        Equivalent + ">(value,cast<" + Equivalent + ">(7));" + Equivalent + " n=5;"
			                                                                            "for(int i=0;i<info.method_count();++i){method_info<Value> method(info.qualified_name,i);"
			                                                                            "if(method.name()==\"Get\")method.invoke<" +
			        Equivalent + "&>(value)+=cast<" + Equivalent + ">(2);"
			                                                       "if(method.name()==\"Read\"){const " +
			        Equivalent + "& view=method.invoke<const " + Equivalent +
			        "&>(value);print(view);}if(method.name()==\"Bump\"){method.invoke<void>(value,&n);print(n);}}}",
			    "fixed-width-scalar-reflection.vs");
			Expect(Run(ScalarReflection, "1\n9\n6\n"),
			    "fixed-width reflection aliases preserve field access, ref returns, const refs and argument storage");
		}
		FCompileOptions SeparateTargets = VeroScript::Test::MakeCompileOptions();
		SeparateTargets.SourceOverlays = {
		    {"int-base.vs", "using Number=int;export class B{public:virtual Number operator Number(){return 1;}};"},
		    {"double-child.vs", "import \"int-base.vs\";using Number=double;"
		                        "export class D:public B{public:Number operator Number(){return 2.5;}};"}};
		auto DistinctConversions = VeroScriptCompile::CompileSource(
		    "import \"double-child.vs\";void main(){D d=D();int n=d;double f=d;print(n);print(f);}",
		    "distinct-targets.vs", SeparateTargets);
		Expect(Run(DistinctConversions, "1\n2.5\n"),
		    "private conversion aliases in separate modules retain distinct target identities");
		for (const auto& Source : {
		         "using Number=int;class C{public:virtual Number operator Number(){return 1;}"
		         "virtual int operator int(){return 2;}};void main(){}",
		         "class C<T>{public:T operator T(){return 1;}int operator int(){return 2;}};"
		         "void main(){C<int> c=C<int>();}"})
		{
			auto Duplicate = VeroScriptCompile::CompileSource(Source, "duplicate-conversion.vs");
			Expect(!Duplicate.bSuccess && Duplicate.Error.Message.find("ambiguous") != std::string::npos,
			    "duplicate canonical conversion targets are rejected before virtual dispatch, including instantiation");
		}
		for (const auto& Source : {
		         "using Number=int;using Other=int;class B{public:virtual Number operator Number(){return 1;}};"
		         "class D:public B{public:Other operator Other() override{return 2;}};"
		         "void main(){D d=D();B b=d;int n=b;print(n);}",
		         "using Number=int;class B{public:virtual Number operator Number()=0;};"
		         "class D:public B{public:int operator int() override{return 2;}};"
		         "void main(){D d=D();B b=d;int n=b;print(n);}",
		         "class B<T>{public:virtual T operator T(){return 1;}};"
		         "class D:public B<int>{public:int operator int() override{return 2;}};"
		         "void main(){D d=D();B<int> b=d;int n=b;print(n);}"})
		{
			auto VirtualConversion = VeroScriptCompile::CompileSource(Source, "virtual-conversion.vs");
			Expect(Run(VirtualConversion, "2\n"),
			    "virtual conversion overrides match canonical target types, including pure and generic bases");
		}
		for (const auto& Kind : {std::string("class B{public:"), std::string("interface B{")})
		{
			auto ReflectedConversion = VeroScriptCompile::CompileSource(
			    "using Number=int;using Other=int;" + Kind + "virtual Number operator Number()=0;};"
			                                                 "class D:public B{public:Other operator Other() override{return 2;}};"
			                                                 "void main(){D d=D();type<B> info=type<B>();for(int i=0;i<info.method_count();++i){"
			                                                 "method_info<B> method(info.qualified_name,i);if(method.name()==\"operator conv Number\")"
			                                                 "print(method.invoke<int>(d));}}",
			    "reflected-conversion.vs");
			Expect(Run(ReflectedConversion, "2\n"), "reflection preserves conversion names and dispatches canonical overrides");
		}
		for (const bool ReadOnly : {false, true})
		{
			FHostBindingRegistry Registry;
			FHostTypeId Id;
			std::string Error;
			Expect(Registry.RegisterType("Host", EHostTypeKind::Class, Id, Error), "register host conversion type");
			FHostMemberDesc Conversion;
			Conversion.Name = "operator conv int";
			Conversion.ReturnType = ENativeType::Int;
			Conversion.bOperator = true;
			Conversion.bConstMethod = ReadOnly;
			Conversion.Callback = [](const FNativeCallContext&, FValue& Out, std::string&)
			{
				Out = std::int32_t{1};
				return true;
			};
			Expect(Registry.RegisterMember(Id, Conversion, Error), "register host conversion method");
			FCompileOptions HostOptions = VeroScript::Test::MakeCompileOptions();
			HostOptions.Bindings = &Registry;
			auto HostConversion = VeroScriptCompile::CompileSource(
			    "int Read(const Host& obj){return obj;}", "host-const-conversion.vs", HostOptions);
			Expect(HostConversion.bSuccess == ReadOnly, "host conversion selection honors const method metadata");
			FHostMemberDesc Add = Conversion;
			Add.Name = "operator+";
			Add.ParamTypes = {ENativeType::Int};
			Expect(Registry.RegisterMember(Id, Add, Error), "register host binary operator");
			FHostMemberDesc Negate = Conversion;
			Negate.Name = "operator-";
			Expect(Registry.RegisterMember(Id, Negate, Error), "register host unary operator");
			for (const auto* Source : {
			         "int Read(const Host& obj){return obj+1;}",
			         "int Read(const Host& obj){return 1+obj;}",
			         "int Read(const Host& obj){return -obj;}"})
			{
				auto Operator = VeroScriptCompile::CompileSource(Source, "host-const-operator.vs", HostOptions);
				Expect(Operator.bSuccess == ReadOnly, "host unary and binary operators honor receiver constness");
			}
			if (ReadOnly)
			{
				FHostMemberDesc Compound = Add;
				Compound.Name = "operator+=";
				Compound.ReturnType = ENativeType::Void;
				Compound.bConstMethod = false;
				Compound.bMutableThis = true;
				Expect(Registry.RegisterMember(Id, Compound, Error), "register host compound operator");
				for (const bool ConstConversion : {false, true})
				{
					const std::string Converter = "struct Convert{int value;int operator int()" +
					    std::string(ConstConversion ? " const" : "") + "{return 7;}};";
					for (const auto* Body : {
					         "int Read(Host& obj,const Convert& operand){return obj+operand;}",
					         "int Read(Host& obj,const Convert& operand){return operand+obj;}",
					         "void Read(Host& obj,const Convert& operand){obj+=operand;}"})
					{
						auto OperandConversion = VeroScriptCompile::CompileSource(
						    Converter + Body, "host-const-operand.vs", HostOptions);
						Expect(OperandConversion.bSuccess == ConstConversion,
						    "host binary and compound operand conversions preserve source constness");
					}
				}
			}
		}
		for (const bool ConstConversion : {false, true})
		{
			const std::string Source = "struct Convert{int operator int()" +
			    std::string(ConstConversion ? " const" : "") + "{return 7;}};"
			                                                   "struct Acc{int value;void operator+=(int n){value+=n;}};"
			                                                   "struct Storage{Acc field;};Acc global=Acc();";
			for (const auto* Body : {
			         "void Read(const Convert& operand){Acc a=Acc();a+=operand;}",
			         "void Read(const Convert& operand){Storage s=Storage();s.field+=operand;}",
			         "void Read(const Convert& operand){array<Acc> items=array<Acc>();items.push(Acc());items[0]+=operand;}",
			         "void Read(const Convert& operand){global+=operand;}",
			         "void Read(Acc& target,const Convert& operand){target+=operand;}"})
			{
				auto Compound = VeroScriptCompile::CompileSource(Source + Body, "compound-const-operand.vs");
				Expect(Compound.bSuccess == ConstConversion,
				    "compound conversion preserves constness across local, field, container, global and reference targets");
			}
			auto FreeCompound = VeroScriptCompile::CompileSource(
			    "struct Convert{int operator int()" + std::string(ConstConversion ? " const" : "") +
			        "{return 7;}};class Acc{public:int value;};"
			        "void operator+=(Acc target,int n){target.value+=n;}"
			        "void Read(Acc& target,const Convert& operand){target+=operand;}",
			    "free-compound-const-operand.vs");
			Expect(FreeCompound.bSuccess == ConstConversion, "free compound conversion preserves source constness");
			if (ConstConversion)
			{
				auto RuntimeCompound = VeroScriptCompile::CompileSource(Source +
				        "void Add(Acc& target,const Convert& operand){target+=operand;}"
				        "Acc& Target(Acc& target){return target;}"
				        "const Convert& Operand(const Convert& operand){return operand;}"
				        "void main(){const Convert operand=Convert();Acc a=Acc();a+=operand;"
				        "Add(ref a,ref operand);Storage s=Storage();s.field+=operand;"
				        "array<Acc> items=array<Acc>();items.push(Acc());items[0]+=operand;"
				        "global+=operand;Target(ref a)+=Operand(ref operand);"
				        "print(a.value);print(s.field.value);print(items[0].value);print(global.value);}",
				    "compound-const-runtime.vs");
				Expect(Run(RuntimeCompound, "21\n7\n7\n7\n"),
				    "const conversions execute and compound writeback retains each target's identity");
			}
		}
	}
	{
		auto OverloadOptions = Options();
		OverloadOptions.SourceOverlays = {
		    {"overload-decl.vs", "export class Overloaded{public:int Get();string Get() const;};"},
		    {"overload-impl.vs", "import \"overload-decl.vs\"; int Overloaded::Get(){return 7;} "
		                         "string Overloaded::Get() const{return \"const\";}"}};
		auto Overloads = VeroScriptCompile::CompileSource(
		    "import \"overload-decl.vs\";import \"overload-impl.vs\";"
		    "void main(){Overloaded m=Overloaded();const Overloaded c=Overloaded();print(m.Get());print(c.Get());}",
		    "overload-main.vs", OverloadOptions);
		Expect(Run(Overloads, "7\nconst\n"), "cross-file const overloads retain distinct return types");
		if (Overloads.bSuccess)
		{
			std::string Error;
			auto Normalized = PrepareCodegenProgram(Overloads.PreparedProgram->GetProgram(), {}, nullptr, Error);
			Expect(bool(Normalized), "normalize cross-file const overloads for code generation");
			if (Normalized)
			{
				VeroScriptCompiler Compiler;
				FCompileResult Recompiled;
				Recompiled.Module = Compiler.Compile(*Normalized);
				Recompiled.bSuccess = bool(Recompiled.Module) && !Compiler.HasError();
				Recompiled.Error = Compiler.GetError();
				Expect(Run(Recompiled, "7\nconst\n"),
				    "code generation preparation preserves overload selection and return types");
			}
		}
	}
	{
		auto InheritedBounds = VeroScriptCompile::CompileSource(
		    "interface I{}; struct S:public I{int value=7;};"
		    "template<T> T Echo(T value) where T:I; template<T> T Echo(T input){return input;}"
		    "void main(){S s=S();print(Echo<S>(s).value);}",
		    "bounds-inherit.vs");
		Expect(Run(InheritedBounds, "7\n"), "same-file definitions inherit omitted prototype constraints");
		auto InvalidArgument = VeroScriptCompile::CompileSource(
		    "interface I{}; template<T> T Echo(T value) where T:I;"
		    "template<T> T Echo(T input){return input;} void main(){print(Echo<int>(1));}",
		    "bounds-invalid-argument.vs");
		Expect(!InvalidArgument.bSuccess, "omitting definition constraints does not relax argument checking");
		auto Mismatch = VeroScriptCompile::CompileSource(
		    "interface I{}; interface J{}; template<T> T Echo(T value) where T:I;"
		    "template<T> T Echo(T input) where T:J {return input;}",
		    "bounds-mismatch.vs");
		Expect(!Mismatch.bSuccess, "definitions cannot replace prototype constraints");
		FCompileOptions CrossOptions = VeroScript::Test::MakeCompileOptions();
		CrossOptions.SourceOverlays = {
		    {"bounds-decl.vs", "export interface I{};export struct S:public I{int value=9;};"
		                       "namespace API{using Constraint=I;template<T> export T Echo(T value) where T:Constraint;}"},
		    {"bounds-impl.vs", "import \"bounds-decl.vs\";template<T> T API::Echo(T input){return input;}"}};
		auto CrossBounds = VeroScriptCompile::CompileSource(
		    "import \"bounds-decl.vs\";import \"bounds-impl.vs\";void main(){S s=S();print(API::Echo<S>(s).value);}",
		    "bounds-main.vs", CrossOptions);
		Expect(Run(CrossBounds, "9\n"), "cross-file definitions inherit constraints from the declaration module");
		auto CrossInvalid = VeroScriptCompile::CompileSource(
		    "import \"bounds-decl.vs\";import \"bounds-impl.vs\";void main(){print(API::Echo<int>(1));}",
		    "bounds-invalid.vs", CrossOptions);
		Expect(!CrossInvalid.bSuccess, "cross-file inherited constraints remain enforced");
		auto BoundsLibrary = VeroScriptCompile::CompileSource(
		    "export import \"bounds-decl.vs\";import \"bounds-impl.vs\";", "bounds-library.vs", CrossOptions);
		FPackageBuildOptions BoundsBuild;
		BoundsBuild.Kind = EPackageKind::Library;
		BoundsBuild.PackageName = "Bounds";
		std::vector<std::uint8_t> BoundsBytes;
		std::string BoundsError;
		const bool PackagedBounds = BoundsLibrary.bSuccess &&
		    BuildPackage(*BoundsLibrary.Module, BoundsBuild, BoundsBytes, BoundsError);
		Expect(PackagedBounds, "package uninstantiated generic functions with inherited constraints");
		if (PackagedBounds)
		{
			const auto Dir = std::filesystem::temp_directory_path() /
			    ("veroscript-bounds-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Dir);
			const auto File = Dir / "Bounds.vslib";
			{
				std::ofstream Out(File, std::ios::binary);
				Out.write(reinterpret_cast<const char*>(BoundsBytes.data()), BoundsBytes.size());
			}
			auto PackageBounds = VeroScriptCompile::CompileSource(
			    "import \"Bounds.vslib\";void main(){S s=S();print(API::Echo<S>(s).value);}",
			    (Dir / "consumer.vs").generic_string());
			Expect(Run(PackageBounds, "9\n"), "packaged functions preserve inherited constraints and definitions");
			auto PackageInvalid = VeroScriptCompile::CompileSource(
			    "import \"Bounds.vslib\";void main(){print(API::Echo<int>(1));}",
			    (Dir / "invalid.vs").generic_string());
			Expect(!PackageInvalid.bSuccess, "package consumers cannot bypass inherited generic constraints");
			std::error_code Error;
			std::filesystem::remove_all(Dir, Error);
		}
		for (const char* Default : {"int", "string"})
		{
			auto RepeatedDefault = VeroScriptCompile::CompileSource(
			    std::string("template<T=int> T Echo(T value); template<T=") + Default +
			        "> T Echo(T input){return input;}",
			    "defaults-repeat.vs");
			Expect(!RepeatedDefault.bSuccess,
			    "same-file definitions cannot repeat or replace prototype type defaults");
		}
		auto DefaultedPrototype = VeroScriptCompile::CompileSource(
		    "template<T=int> T Echo(){return 7;}", "standalone-default.vs");
		Expect(DefaultedPrototype.bSuccess, "standalone generic definitions retain type defaults");
		auto DefaultedDefinition = VeroScriptCompile::CompileSource(
		    "template<T=int> T Echo(); template<T> T Echo(){return 7;} void main(){print(Echo());}",
		    "defaults-inherit.vs");
		Expect(Run(DefaultedDefinition, "7\n"), "supplemental definitions inherit prototype type defaults");
		auto Reordered = VeroScriptCompile::CompileSource(
		    "interface I{}; interface J{}; template<T> T Echo(T value) where "
		    "T:I,J; "
		    "template<T> T Echo(T value) where T:J,I {return value;}",
		    "bounds-order.vs");
		if (!Reordered.bSuccess)
		{
			std::cerr << Reordered.Error.Message << '\n';
		}

		Expect(Reordered.bSuccess, "equivalent generic constraints do not depend on written order");
	}
	auto Missing = VeroScriptCompile::CompileSource("int Missing(int a); void main(){}", "missing.vs");
	Expect(!Missing.bSuccess, "unused unimplemented prototypes fail compilation");
	auto Conflict = VeroScriptCompile::CompileSource(
	    "struct A { @Getter int x; int get_x() const {return 1;} };", "conflict.vs", Options());
	Expect(!Conflict.bSuccess, "generation cannot overwrite user methods");
	auto Meta = VeroScriptCompile::CompileSource("@Unknown(flag, text=\"\", meta=(yes, "
	                                             "count=-4)) struct A {}; void main(){}",
	    "metadata.vs");
	Expect(Meta.bSuccess, "unregistered annotations remain metadata");
	if (Meta.bSuccess)
	{
		const auto& A = Meta.PreparedProgram->GetProgram().Structs.front().DeclMeta.Annotations.front();
		Expect(A.Name == "@Unknown" && A.Arguments.size() == 3 &&
		        A.Arguments[1].ScalarKind == EAnnotationScalarKind::String &&
		        A.Arguments[1].Value.empty() && A.Arguments[2].Children.size() == 2,
		    "structured arguments preserve groups and scalar kinds");
	}
	VeroScriptModuleLoader Loader("");
	auto Input = Loader.LoadSourceModule("struct A { @Getter int x=3; }; void main(){}", "immutable.vs");
	auto Prepared = VeroScriptProgramPreparation::Prepare(*Input, Options().AnnotationProcessing);
	Expect(Prepared.Succeeded() && Input->Structs.front().Methods.empty(),
	    "preparation does not mutate borrowed AST");
	if (Prepared.Succeeded())
	{
		VeroScriptCompiler C;
		Expect(bool(C.CompilePrepared(*Prepared.Prepared)) && bool(C.CompilePrepared(*Prepared.Prepared)),
		    "prepared results compile repeatedly without invoking processors");
	}
	if (Prepared.Succeeded())
	{
		auto Same = VeroScriptProgramPreparation::Prepare(Prepared.Prepared->GetProgram());
		Expect(Same.Succeeded() && Same.Prepared->GetFingerprint() == Prepared.Prepared->GetFingerprint() &&
		        Same.Prepared->GetGeneratedOperations().size() ==
		            Prepared.Prepared->GetGeneratedOperations().size(),
		    "prepared clones preserve generation and cache identity");
		std::vector<FNativeFunction> Changed(1);
		Changed[0].Name = "changed";
		auto Reprepare = VeroScriptProgramPreparation::Prepare(Prepared.Prepared->GetProgram(), {}, &Changed);
		Expect(!Reprepare.Succeeded(), "repreparation rejects changed native configuration");
	}

	{
		const auto AddProcessor =
		    [](std::string Name, std::function<void(const FAnnotationContext&, FAnnotationOutput&)> Callback,
		        std::uint32_t Targets = AnnotationAllTargets)
		{
			struct FCallback : IAnnotationProcessor
			{
				void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
				{
					Fn(C, O);
				}

				// Fn precedes the constructor because its parameter uses decltype(Fn).
				std::function<void(const FAnnotationContext&, FAnnotationOutput&)> Fn;
				explicit FCallback(decltype(Fn) F) : Fn(std::move(F))
				{
				}
			};
			auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
			std::string Error;
			Expect(Registry->Register({std::move(Name), "test.callback", "1", "", Targets, 3, 0,
			                              [Callback]
			{
				return std::make_unique<FCallback>(Callback);
			}},
			           Error),
			    "register callback");
			Registry->Freeze();
			FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
			Options.AnnotationProcessing.Registry = Registry;
			return Options;
		};
		auto ChainOptions = AddProcessor("@Fluent",
		    [](const auto& C, auto& O)
		{
			O.SubmitMethod(
			    C.Owner->Id, EMemberAccess::Public, "Builder& With(int input){value=input;return this;}");
		});
		auto Chain =
		    VeroScriptCompile::CompileSource("struct Builder{@Fluent int value;}; void main(){Builder "
		                                     "b=Builder(); b.With(3).With(8); print(b.value);}",
		        "chain.vs", ChainOptions);
		Expect(Run(Chain, "8\n"), "generated reference return supports ordinary chaining");
		auto StaticOptions = AddProcessor(
		    "@StaticGetter",
		    [](const auto& C, auto& O)
		{
			O.SubmitMethod(
			    C.Owner->Id, EMemberAccess::Public, "int Read(){return " + C.Owner->Name + "::value;}");
		},
		    AnnotationTargetMask(EAnnotationTarget::StaticField));
		auto Static = VeroScriptCompile::CompileSource(
		    "static class Settings{@StaticGetter int value=3;}; static struct "
		    "More{@StaticGetter int value=4;}; "
		    "void main(){print(Settings::Read()+More::Read());}",
		    "static.vs", StaticOptions);
		Expect(Run(Static, "7\n"), "generated methods inherit static class and struct semantics");
		auto StaticVirtualOptions = AddProcessor(
		    "@InvalidStatic",
		    [](const auto& C, auto& O)
		{
			O.SubmitMethod(C.Owner->Id, EMemberAccess::Public, "virtual int Read(){return value;}");
		},
		    AnnotationTargetMask(EAnnotationTarget::StaticField));
		Expect(!VeroScriptCompile::CompileSource("static class Settings{@InvalidStatic int value=3;};",
		           "invalid-static.vs", StaticVirtualOptions)
		           .bSuccess,
		    "generated static-container methods reject instance modifiers");
		auto BoundsQuery = AddProcessor("@InspectBounds",
		    [](const auto& C, auto&)
		{
			Expect(C.Declaration.TypeParameters.size() == 1 &&
			        C.Declaration.TypeParameterDefaults.size() == 1 &&
			        C.Declaration.TypeParameterDefaults[0] == "S" &&
			        C.Declaration.TypeBounds.size() == 1 &&
			        C.Declaration.TypeBounds[0].CanonicalInterfaceTypeName == "I" &&
			        C.Declaration.BaseAccess.size() == 1 &&
			        C.Declaration.BaseAccess[0] == EMemberAccess::Protected,
			    "processor can inspect defaults, canonical constraints and "
			    "inheritance access");
		});
		auto BoundsViewed = VeroScriptCompile::CompileSource(
		    "export interface I{}; struct S:I{}; class Base{}; @InspectBounds "
		    "class Box<T=S> : protected Base where T:I {public:T value;};",
		    "bounds-view.vs", BoundsQuery);
		if (!BoundsViewed.bSuccess)
		{
			std::cerr << BoundsViewed.Error.Message << '\n';
		}

		Expect(BoundsViewed.bSuccess, "generic declaration snapshots retain query details");
		{
			struct FMulti : IAnnotationProcessor
			{
				void Process(const FAnnotationContext& C, FAnnotationOutput& O) override
				{
					Calls.push_back(Name);
					Expect(std::none_of(C.Declarations.begin(), C.Declarations.end(),
					           [](const auto& D)
					{
						return D.bGenerated;
					}),
					    "handlers see the same original declaration snapshot");
					O.SubmitMethod(C.Declaration.Id, EMemberAccess::Public, "int " + Name + "(){return 1;}");
				}

				FMulti(std::string N, std::vector<std::string>& C) : Name(std::move(N)), Calls(C)
				{
				}

				std::string Name;

				std::vector<std::string>& Calls;
			};
			std::vector<std::string> Calls;
			auto Registry = std::make_shared<FAnnotationProcessorRegistry>();
			std::string Error;
			for (auto Name : {std::string("Second"), std::string("First")})
			{
				Expect(Registry->Register(
				           {"@Multi", Name, "1", "", AnnotationTargetMask(EAnnotationTarget::Struct), 1, 0,
				               [Name, &Calls]
				{
					return std::make_unique<FMulti>(Name, Calls);
				}},
				           Error),
				    "register multiple handlers");
			}

			Registry->Freeze();
			FCompileOptions MultiOptions = VeroScript::Test::MakeCompileOptions();
			MultiOptions.AnnotationProcessing.Registry = Registry;
			auto Multi = VeroScriptCompile::CompileSource(
			    "@Multi struct A{};void main(){A a=A();print(a.First()+a.Second());}", "multi.vs",
			    MultiOptions);
			Expect(Run(Multi, "2\n") && Calls == std::vector<std::string>{"First", "Second"},
			    "multiple processors run deterministically and their outputs are "
			    "merged once");
		}
		auto LookupOptions = AddProcessor("@Lookup",
		    [](const auto& C, auto& O)
		{
			O.SubmitMethod(C.Owner->Id, EMemberAccess::Public, "int Read(){return Helper();}");
		});
		auto Lookup = VeroScriptCompile::CompileSource(
		    "namespace N{int Helper(){return 7;}}\nusing namespace N;\nstruct "
		    "A{@Lookup int value;};\nvoid main(){A a=A();print(a.Read());}",
		    "lookup.vs", LookupOptions);
		Expect(Run(Lookup, "7\n"), "generated body uses annotation lexical lookup point");
		auto LateUsing =
		    VeroScriptCompile::CompileSource("namespace N{int Helper(){return 7;}}\nstruct A{@Lookup int "
		                                     "value;};\nusing namespace N;",
		        "late.vs", LookupOptions);
		Expect(!LateUsing.bSuccess, "generated body cannot see a later using directive");
		auto Dependencies = AddProcessor("@Dependency",
		    [](const auto& C, auto&)
		{
			std::string Content;
			C.ReadDependency("present", Content);
			C.ReadDependency("missing", Content);
		});
		int Revision = 1;
		Dependencies.AnnotationProcessing.ReadDependency = [&](std::string_view Path, std::string& Text)
		{
			if (Path == "present")
			{
				Text = std::to_string(Revision);
				return true;
			}
			return false;
		};
		auto D1 =
		    VeroScriptCompile::CompileSource("@Dependency struct A{};", "dependencies.vs", Dependencies);
		++Revision;
		auto D2 =
		    VeroScriptCompile::CompileSource("@Dependency struct A{};", "dependencies.vs", Dependencies);
		Expect(D1.bSuccess && D2.bSuccess && D1.PreparedProgram->GetDependencies().size() == 2 &&
		        !D1.PreparedProgram->GetDependencies()[1].bExists &&
		        D1.PreparedProgram->GetFingerprint() != D2.PreparedProgram->GetFingerprint(),
		    "present and missing dependencies affect preparation identity");
		auto BadOptions = AddProcessor("@Broken",
		    [](const auto& C, auto& O)
		{
			O.SubmitMethod(C.Owner->Id, EMemberAccess::Public, "int Broken(){ return missing; }");
		});
		auto Bad = VeroScriptCompile::CompileSource("struct A{@Broken int value;};", "origin.vs", BadOptions);
		Expect(!Bad.bSuccess && Bad.Error.Location.FileLabel == "origin.vs",
		    "generated body diagnostics point to original annotation");
		auto Failure = AddProcessor("@Fail",
		    [](const auto&, auto&)
		{
			throw std::runtime_error("specific failure");
		});
		auto Failed = VeroScriptCompile::CompileSource("@Fail struct A {};", "failure.vs", Failure);
		Expect(!Failed.bSuccess && Failed.Error.Message.find("specific failure") != std::string::npos,
		    "processor exceptions become diagnostics");
		auto Warning = AddProcessor("@Warning",
		    [](const auto&, auto& O)
		{
			FScriptError D;
			D.Message = "nonfatal warning";
			D.Severity = ECompileDiagnosticSeverity::Warning;
			O.ReportDiagnostic(std::move(D));
		});
		auto Warned =
		    VeroScriptCompile::CompileSource("@Warning struct A{};void main(){}", "warning.vs", Warning);
		Expect(Warned.bSuccess && Warned.Diagnostics.size() == 1 &&
		        Warned.Diagnostics[0].Severity == ECompileDiagnosticSeverity::Warning,
		    "warnings do not fail compilation");
		int Targets = 0;
		auto Query = AddProcessor("@Inspect",
		    [&](const auto& C, auto&)
		{
			++Targets;
			if (C.Declaration.Target == EAnnotationTarget::Field)
			{
				Expect(C.Declaration.DefaultValueSource == "7", "field initializer snapshot");
			}
		});
		auto Queried = VeroScriptCompile::CompileSource(
		    "@Inspect int global=3; @Inspect enum E{@Inspect X}; @Inspect class "
		    "A{public:@Inspect int "
		    "x=7;@Inspect "
		    "static int y=2;@Inspect A(){} @Inspect ~A(){} @Inspect int f(int "
		    "a=2){return a;} @Inspect "
		    "static void "
		    "s(){}}; @Inspect void F(){} @Inspect struct S{}; @Inspect interface "
		    "I{}; void main(){}",
		    "targets.vs", Query);
		if (!Queried.bSuccess)
		{
			std::cerr << Queried.Error.Message << '\n';
		}

		Expect(Queried.bSuccess && Targets == 13, "all advertised declaration targets are processed");
		auto Budget = Options();
		Budget.AnnotationProcessing.MaxOperations = 0;
		Expect(!VeroScriptCompile::CompileSource("struct A{@Getter int x;};", "budget.vs", Budget).bSuccess,
		    "generation budget is enforced");
		auto Worker = []
		{
			auto R = VeroScriptCompile::CompileSource(
			    "struct A{@Getter int x=6;};void main(){A a=A();print(a.get_x());}", "parallel.vs",
			    Options());
			return R.bSuccess;
		};
		auto W1 = std::async(std::launch::async, Worker);
		auto W2 = std::async(std::launch::async, Worker);
		Expect(W1.get() && W2.get(), "independent preparation tasks are thread safe");
	}

	{
		auto PackageOptions = Options();
		PackageOptions.SourceOverlays = {
		    {"package-decl.vs", "export struct Box<T>{ @Getter T value; Box(T x){value=x;} T Read() "
		                        "const; }; "
		                        "export class Closed { public: @Getter int value=19; }; namespace "
		                        "API{template<T> export T Echo(T input);}"},
		    {"package-impl.vs", "import \"package-decl.vs\"; T Box<T>::Read() const{return value;} "
		                        "template<T> T API::Echo(T value){return value;}"}};
		auto PackageSource = VeroScriptCompile::CompileSource(
		    "export import \"package-decl.vs\"; export import \"package-impl.vs\";", "producer.vs",
		    PackageOptions);
		Expect(PackageSource.bSuccess, "compile exported generated generic");
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		FPackageBuildOptions Build;
		Build.Kind = EPackageKind::Library;
		Build.PackageName = "Annotated";
		Build.PackageVersion = 1;
		Build.bStripDebugInfo = true;
		bool Packaged = PackageSource.bSuccess && BuildPackage(*PackageSource.Module, Build, Bytes, Error);
		if (!Packaged)
		{
			std::cerr << "package: " << Error << '\n';
		}

		Expect(Packaged, "build portable generated generic package");
		if (Packaged)
		{
			FPackageData Data;
			Expect(ReadPackage(FByteView(Bytes), Data, Error), "read generated package");
			const auto Dir = std::filesystem::temp_directory_path() /
			    ("vero_annotation_" +
			        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Dir);
			const auto File = Dir / "Annotated.vslib";
			{
				std::ofstream Out(File, std::ios::binary);
				Out.write(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
			}
			FCompileOptions Consumer = VeroScript::Test::MakeCompileOptions();
			const auto ImportDir = Dir.generic_string();
			Consumer.ImportPaths.push_back(ImportDir);
			auto Import = VeroScriptCompile::CompileSource(
			    "import \"Annotated.vslib\"; void main(){ Closed c=Closed(); "
			    "print(c.get_value()); Box<int> "
			    "b(17); "
			    "print(b.get_value()); Box<string> s(\"pkg\"); print(s.get_value()); "
			    "print(b.Read()); print(API::Echo<int>(8)); }",
			    "consumer.vs", Consumer);
			Expect(Run(Import, "19\n17\npkg\n17\n8\n"),
			    "consumer instantiates generated and cross-file generics without "
			    "producer or processors");
			auto Reexport = VeroScriptCompile::CompileSource("export import \"Annotated.vslib\";",
			    "reexport.vs", Consumer);
			Build.PackageName = "Wrapper";
			Bytes.clear();
			bool Repacked = Reexport.bSuccess && BuildPackage(*Reexport.Module, Build, Bytes, Error);
			if (!Repacked)
			{
				std::cerr << "repack: " << (Reexport.bSuccess ? Error : Reexport.Error.Message) << '\n';
			}

			Expect(Repacked, "re-export preserves generation and original component ownership");
			if (Repacked)
			{
				const auto Wrapper = Dir / "Wrapper.vslib";
				{
					std::ofstream Out(Wrapper, std::ios::binary);
					Out.write(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
				}
				auto Again = VeroScriptCompile::CompileSource(
				    "import \"Wrapper.vslib\"; void main(){ Box<double> b(2.5); "
				    "print(b.get_value()); print(b.Read()); "
				    "print(API::Echo<string>(\"ok\")); }",
				    "next.vs", Consumer);
				Expect(Run(Again, "2.5\n2.5\nok\n"),
				    "repacked cross-file generic remains usable with new arguments");
			}
			std::filesystem::remove(File);
			std::filesystem::remove(Dir / "Wrapper.vslib");
			std::filesystem::remove(Dir);
		}
	}

	try
	{
		auto Cancel = Options();
		Cancel.AnnotationProcessing.CheckCancelled = []
		{
			throw FAnalysisCancelled();
		};
		VeroScriptCompile::CompileSource("struct A { @Getter int x; };", "cancel.vs", Cancel);
		Expect(false, "cancellation must propagate");
	}
	catch (const FAnalysisCancelled&)
	{
	}
	{
		std::string Source = "struct Many {";
		for (int I = 0; I < 300; ++I)
		{
			Source += "int field" + std::to_string(I) + "=1;";
		}

		Source += "};";
		VeroScriptModuleLoader CloneLoader("");
		auto Original = CloneLoader.LoadSourceModule(Source, "clone-cancel.vs");
		bool Cancelled = false;
		int Checks = 0;
		{
			FAnalysisControlScope Scope(
			    [&]
			{
				return ++Checks >= 20;
			});
			try
			{
				VeroScriptProgramPreparation::Prepare(*Original);
			}
			catch (const FAnalysisCancelled&)
			{
				Cancelled = true;
			}
		}
		Expect(Cancelled && !Original->bDeclarationsPrepared && Original->Structs[0].Fields.size() == 300,
		    "borrowed AST cloning cooperates with cancellation without mutating "
		    "its input");
		Cancelled = false;
		Checks = 0;
		FAnnotationProcessingOptions DirectControl;
		DirectControl.CheckCancelled = [&]
		{
			if (++Checks >= 20)
			{
				throw FAnalysisCancelled();
			}
		};
		try
		{
			VeroScriptProgramPreparation::Prepare(*Original, DirectControl);
		}
		catch (const FAnalysisCancelled&)
		{
			Cancelled = true;
		}
		Expect(Cancelled, "explicit preparation control also cancels borrowed AST cloning");
		Expect(VeroScriptProgramPreparation::Prepare(*Original).Succeeded(),
		    "a cancelled clone does not poison subsequent preparation");
	}
	return Failures ? 1 : 0;
}
