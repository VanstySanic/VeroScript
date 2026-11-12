# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

file(MAKE_DIRECTORY "${WORK}")
function(require_success label)
	if(NOT status EQUAL 0)
		message(FATAL_ERROR "${label} failed (${status})\n${out}\n${err}")
	endif()
endfunction()
set(samples attribute_functions attribute_methods attribute_contract)
if(KIND STREQUAL "cpp")
	list(APPEND samples attribute_weak)
	set(extension cpp)
elseif(KIND STREQUAL "csharp")
	set(extension cs)
	execute_process(COMMAND "${DOTNET}" --version OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE)
	string(REGEX MATCH "^[0-9]+" major "${version}")
	set(framework "net${major}.0")
else()
	set(extension py)
endif()
file(WRITE "${WORK}/attribute_contract.vs" [=[
import "attribute.vs";
int reads=0;
int read(){reads+=1;return 17;}
bool read_true(){reads+=1;return true;}
optional<int> unavailable(){return optional<int>();}
attribute<T> identity_attribute<T>(T value){return attribute<T>(value);}
struct Item{public:int value;Item(int v){value=v;}};
class Model{public:int value=3;};
class Base{public:int read(){return 1;}};
class Derived:public Base{public:int read(){return 4;}};
int main(){
    auto generic=identity_attribute<int>(7);assert(generic()==7);
    std::attribute<int> a=4;std::attribute<int> copy=a;assert(a.has_same_configuration(ref copy));copy.set(6);assert(!a.has_same_configuration(ref copy) && a()==4 && copy()==6);
    a.bind(read);assert(reads==0);int converted=a;assert(converted==17 && reads==1);
    std::attribute<bool> flag=true;flag.bind(read_true);if(flag){assert(reads==2);}
    assert(true || flag);assert(reads==2);assert(flag && true);assert(reads==3);assert(!(!flag));assert(reads==4);
    a.bind_optional(unavailable);assert(a.is_bound() && !a.try_get().has_value() && a.get_or(9)==9);
    std::attribute<optional<int>> nested=std::attribute<optional<int>>();assert(!nested.try_get().has_value());
    nested.set(optional<int>());assert(nested.try_get().has_value() && !nested.get().has_value());
    std::attribute<Item> item=std::attribute<Item>();assert(!item.is_set());item.set(Item(8));Item value=item.get();value.value=9;assert(item().value==8);
    std::attribute<Model> null_value=nullptr;assert(null_value.is_set() && null_value.get()==nullptr);
    std::attribute<fn()->int> callback=read;fn()->int f=callback;assert(f()==17);
    Derived target=Derived();auto base=std::bind_method<fn()->int,Base>(target,"read");assert(base()==1);
    a.reset();assert(!a.is_set() && !a.is_bound());print("attribute contract: ok");return 0;
}
]=])
if(KIND STREQUAL "cpp")
	list(APPEND samples attribute_reentry)
	file(WRITE "${WORK}/attribute_reentry.vs" [=[
import "attribute.vs";
class Owner{public:std::attribute<Payload> value;};
Owner owner=Owner();int releases=0;
class Payload{public:~Payload(){releases+=1;assert(owner.value.is_bound());owner.value.reset();}};
Payload read_payload(){return nullptr;}
int main(){owner.value.set(Payload());owner.value.bind(read_payload);assert(releases==1 && !owner.value.is_set());print("attribute reentry: ok");return 0;}
]=])
endif()
if(NOT KIND STREQUAL "python")
	list(APPEND samples attribute_references)
	file(WRITE "${WORK}/attribute_references.vs" [=[
import "attribute.vs";
class Model{public:int value=4;int modify(ref int n){n+=value;return n;}};
int main(){Model model=Model();auto f=std::bind_method<fn(ref int)->int>(model,"modify");int n=3;assert(f(ref n)==7 && n==7);std::attribute<int> a=std::attribute<int>();assert(!a.try_get(ref n) && n==7);a.set(11);auto found=a.try_get(ref n);assert(found && n==11);a.bind_optional(fn()->optional<int>{return optional<int>();});assert(!a.try_get(ref n) && n==11);print("attribute references: ok");return 0;}
]=])
	list(APPEND samples attribute_qualifiers)
	file(WRITE "${WORK}/attribute_qualifiers.vs" [=[
import "attribute.vs";
class Model{public:int read(){return 2;}int read() const{return 3;}};
int main(){Model model=Model();const Model frozen=model;auto m=std::make_attribute_by_name<int>(model,"read");auto c=std::make_attribute_by_name<int>(frozen,"read");assert(m()==2 && c()==3);print("attribute qualifiers: ok");return 0;}
]=])
endif()
if(KIND STREQUAL "csharp")
	# C# source export already rejects overloads distinguished only by const.
	# Validate both legal receiver kinds with distinct ordinary method names.
	file(READ "${WORK}/attribute_qualifiers.vs" qualifiers)
	string(REPLACE "int read(){return 2;}" "int read_mutable(){return 2;}" qualifiers "${qualifiers}")
	string(REPLACE "(model,\"read\")" "(model,\"read_mutable\")" qualifiers "${qualifiers}")
	file(WRITE "${WORK}/attribute_qualifiers.vs" "${qualifiers}")
	file(READ "${WORK}/attribute_references.vs" refs)
	string(REPLACE "return n;}};" "return n;}int& reference(){return value;}};" refs "${refs}")
	string(REPLACE "assert(f(ref n)==7 && n==7);" "assert(f(ref n)==7 && n==7);auto r=std::bind_method<fn()->int&>(model,\"reference\");r()=11;assert(model.value==11);" refs "${refs}")
	file(WRITE "${WORK}/attribute_references.vs" "${refs}")
endif()
foreach(sample IN LISTS samples)
	if(sample STREQUAL "attribute_contract" OR sample STREQUAL "attribute_reentry" OR sample STREQUAL "attribute_references" OR sample STREQUAL "attribute_qualifiers")
		set(source "${WORK}/${sample}.vs")
	else()
		set(source "${EXAMPLES}/${sample}.vs")
	endif()
	execute_process(COMMAND "${VERO_CLI}" aot "--target=${KIND}" "${source}" -o "${WORK}/${sample}.${extension}"
		RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
	require_success("${KIND} ${sample} export")
endforeach()
if(KIND STREQUAL "cpp")
	file(WRITE "${WORK}/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)\nproject(AttributeAot LANGUAGES CXX)\n")
	foreach(sample IN LISTS samples)
		file(APPEND "${WORK}/CMakeLists.txt" "add_executable(${sample} ${sample}.cpp)\ntarget_compile_features(${sample} PRIVATE cxx_std_17)\n")
	endforeach()
	set(generator_args -G "${GENERATOR}")
	if(PLATFORM)
		list(APPEND generator_args -A "${PLATFORM}")
	endif()
	execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK}" -B "${WORK}/build" ${generator_args}
		RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
	require_success("C++ configure")
	execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK}/build" --config Release --parallel 4
		RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 180)
	require_success("C++ compile")
elseif(KIND STREQUAL "csharp")
	foreach(sample IN LISTS samples)
		file(WRITE "${WORK}/${sample}.csproj" "<Project Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>${framework}</TargetFramework><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include=\"${sample}.cs\" /></ItemGroup></Project>")
		execute_process(COMMAND "${DOTNET}" build "${WORK}/${sample}.csproj" --configuration Release --nologo
			--output "${WORK}/bin/${sample}"
			RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
		require_success("C# ${sample} compile")
	endforeach()
endif()
foreach(sample IN LISTS samples)
	if(KIND STREQUAL "cpp")
		if(EXISTS "${WORK}/build/Release/${sample}${EXE_SUFFIX}")
			set(command "${WORK}/build/Release/${sample}${EXE_SUFFIX}")
		else()
			set(command "${WORK}/build/${sample}${EXE_SUFFIX}")
		endif()
	elseif(KIND STREQUAL "csharp")
		set(command "${DOTNET}" "${WORK}/bin/${sample}/${sample}.dll")
	else()
		set(command "${PYTHON}" "${WORK}/${sample}.py")
	endif()
	execute_process(COMMAND ${command} RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
	require_success("${KIND} ${sample} execution")
	string(REPLACE "\r\n" "\n" out "${out}")
	string(REPLACE "attribute_" "attribute " expected "${sample}")
	if(NOT out STREQUAL "${expected}: ok\n")
		message(FATAL_ERROR "${KIND} ${sample} output mismatch: [${out}]\n${err}")
	endif()
endforeach()
if(KIND STREQUAL "python")
	file(WRITE "${WORK}/attribute_ref_output.vs" [=[
import "attribute.vs";
int main(){std::attribute<int> value=4;int output=0;return value.try_get(ref output)?output:0;}
]=])
	file(REMOVE "${WORK}/attribute_ref_output.py")
	execute_process(COMMAND "${VERO_CLI}" aot --target=python "${WORK}/attribute_ref_output.vs" -o "${WORK}/attribute_ref_output.py"
		RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
	if(status EQUAL 0 OR NOT err MATCHES "writable ref" OR EXISTS "${WORK}/attribute_ref_output.py")
		message(FATAL_ERROR "Python writable attribute ref output must be rejected explicitly: ${out} ${err}")
	endif()
endif()
if(NOT KIND STREQUAL "cpp")
	file(REMOVE "${WORK}/unsupported.${extension}")
	execute_process(COMMAND "${VERO_CLI}" aot "--target=${KIND}" "${EXAMPLES}/attribute_weak.vs" -o "${WORK}/unsupported.${extension}"
		RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
	if(status EQUAL 0 OR NOT err MATCHES "weak" OR EXISTS "${WORK}/unsupported.${extension}")
		message(FATAL_ERROR "${KIND} weak attribute must be rejected explicitly: ${out} ${err}")
	endif()
endif()
