// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export enum class JsonGuard { Ready=1 };

namespace ValueModel
{
export enum class Mode : uint8 { First=1u, Last=255u };

export template<int N=7,typename T=int> struct Base
{
public:
	int count=N;
	T inherited;
};

export template<typename T,int N,bool Enabled> struct Record : public Base<N,T>
{
public:
	T payload;
	bool enabled=Enabled;
	static constexpr int limit=N;
};

export template<Mode Selection> struct Tagged
{
public:
	Mode selected=Selection;
};

export template<uint64 Limit> struct Wide
{
public:
	uint64 value=Limit;
};

export struct _VeroJsonParameter0 { int value=23; };

export template<typename JsonGuard,JsonGuard input,int context,bool success> struct Colliding
{
public:
	_VeroJsonParameter0 fixed;
	JsonGuard payload;
	int total=cast<int>(input)+context;
	bool enabled=success;
};

export struct DefaultDerived : public Base<> {};
export template<int N,typename T> struct Shifted : public Base<(N >> 1),T> {};
export template<int N,typename T> struct Quoted : public Base<'>',T>
{
	int marker=N;
};

export template<int N> struct Number { int count=N; };
export template<int N> class PropertyBase
{
private:
	Number<N*2> stored;
public:
	@JsonProperty(name="child",setter="set") Number<N*2> get() const { return stored; }
	void set(Number<N*2> next) { stored=next; }
};
export template<int N> class PropertyDerived : public PropertyBase<N+1> {};
export template<int N,int A,int B> class ChosenProperty : public PropertyBase<(N>0?A:B)> {};

export using Value=bool;
export class Traits<T> { public: using Value=T; };
export struct TraitRecord<T> { Traits<T>::Value payload; };
}
