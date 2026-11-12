// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

if (string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("VEROSCRIPT_INTEROP_LIBRARY")))
{
	Console.WriteLine(typeof(VeroScript.GeneratedBindings.VeroScriptCSharpBindings).FullName);
	return;
}

using var session = new VeroScriptSession();
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
VeroScriptRunResult loaded = session.LoadSource("""
    int ExerciseGeneratedBinding()
    {
        Demo::AbstractCounter abstractCounter = ::Demo::AbstractCounter.Create();
        if (abstractCounter.Read() != 42) return 0;
        Demo::Counter counter = Demo::Counter(5);
        counter.Value = 8;
        int bumped = 1;
        int assigned = 0;
        counter.Bump(ref bumped);
        counter.Assign(ref assigned);
        array<int> values = {2, 3};
        array<int> echoed = counter.EchoValues(values);
        array<int> arrayValues = {4};
        array<int> echoedArray = counter.EchoArray(arrayValues);
        array<string> textValues = {"text"};
        array<string> echoedText = counter.EchoText(textValues);
        map<string, int> mapValues = {"key": 5};
        map<string, int> echoedMap = counter.EchoMap(mapValues);
        set<int> setValues = {7};
        set<int> echoedSet = counter.EchoSet(setValues);
        map<char, int> byteMapValues = {'\xFF': 6};
        map<char, int> echoedByteMap = counter.EchoSignedByteMap(byteMapValues);
        set<char> byteSetValues = {'\x80'};
        set<char> echoedByteSet = counter.EchoSignedByteSet(byteSetValues);
        queue<int> queueValues = {8};
        queue<int> echoedQueue = counter.EchoQueue(queueValues);
        stack<int> stackValues = {9};
        stack<int> echoedStack = counter.EchoStack(stackValues);
        return counter.Add(4) + bumped + assigned + echoed[1]
            + echoedArray[0] + len(echoedText[0]) + echoedMap["key"]
            + echoedSet.values()[0] + echoedByteMap['\xFF']
            + to_int(echoedByteSet.values()[0])
            + echoedQueue.front() + echoedStack.top();
    }
    """, "generated-binding-runtime.vs");
if (!loaded.Success)
{
	string diagnostics = string.Join(Environment.NewLine,
		Enumerable.Range(0, checked((int)loaded.DiagnosticCount))
			.Select(index => session.FormatDiagnostic((ulong)index)));
	throw new InvalidOperationException("Generated binding test source did not load."
		+ Environment.NewLine + diagnostics);
}
var (value, call) = session.CallFunction("ExerciseGeneratedBinding");
using (value)
{
	int actual = value.GetInt32();
	if (!call.Success || actual != -53)
	{
		throw new InvalidOperationException(
				$"Generated binding callback returned {actual}; expected -53.");
	}
}
if (session.LoadSource("void main(){Demo::AbstractCounter value = Demo::AbstractCounter();}").Success)
{
	throw new InvalidOperationException("Generated abstract hosts must not be constructible from script.");
}
Console.WriteLine("Generated registration runtime test passed.");
