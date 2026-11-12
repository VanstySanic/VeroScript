// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../Processing/VeroScriptSourceMetadataIO.h"
#include "VeroScriptBytecode.h"
#include <iostream>
#include <stdexcept>

using namespace VeroScript;
namespace
{
	void Require(bool Value, const std::string& Message)
	{
		if (!Value)
		{
			throw std::runtime_error(Message);
		}
	}
	void TestRepeatedSourceMetadata()
	{
		FSourceMetadataSnapshot Source;
		const std::string Path = "/project/" + std::string(1024, 'p') + "/界面.vs";
		FSourceDocument Raw;
		Raw.Id = Path + ":raw";
		Raw.ModuleId = Path;
		Raw.Text = "中\néx";
		Raw.LineOffsets = {0, 4};
		Source.Documents.push_back(Raw);
		FSourceDocument Active = Raw;
		Active.Id = Path;
		FSourceOrigin Original;
		Original.Range = {Raw.Id, 0, Raw.Text.size(), {{Raw.Id, 1, 1}, {Raw.Id, 2, 4}}};
		Active.Mappings.push_back({0, Active.Text.size(), Original});
		Source.Documents.push_back(Active);
		FSourceNodeMetadata Root;
		Root.Id = "module:" + Path;
		Root.Kind = "module";
		Root.ModuleId = Path;
		Root.Range = {Path, 0, Active.Text.size(), {{Path, 1, 1}, {Path, 2, 4}}};
		Source.Nodes.push_back(Root);
		FAnnotationDefinition Definition;
		Definition.Id = "gui-widget";
		Definition.ProviderId = "gui";
		Definition.Name = "@GuiWidget";
		Source.Definitions.push_back(Definition);
		for (std::size_t Index = 0; Index < 9000; ++Index)
		{
			FSourceNodeMetadata Node;
			Node.Id = "syntax:" + std::to_string(Index);
			Node.OwnerId = Root.Id;
			Node.ParentId = Root.Id;
			Node.Kind = "expression:1";
			Node.ModuleId = Path;
			Node.Range = {Path, 4, 6, {{Path, 2, 1}, {Path, 2, 3}}};
			Node.Origin = {Node.Range, "gui", "generation"};
			FDeclarationAnnotation Annotation;
			Annotation.Name = Definition.Name;
			Annotation.DefinitionId = Definition.Id;
			Annotation.Range = Node.Range.Position;
			Annotation.NamePosition = Annotation.Range.Begin;
			Node.Annotations.push_back(Annotation);
			Source.Nodes.push_back(std::move(Node));
		}
		FSourceComment Comment;
		Comment.Id = "comment";
		Comment.ModuleId = Path;
		Comment.OwnerId = Source.Nodes[1].Id;
		Comment.RawText = Comment.Text = "中";
		Comment.Range = {Path, 0, 3, {{Path, 1, 1}, {Path, 1, 4}}};
		Source.Nodes[1].Comments.push_back(Comment);
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		const bool Encoded = EncodeSourceMetadata(Source, Bytes, Error);
		Require(Encoded, "repeated metadata: " + Error);
		Require(Bytes.size() < 4u * 1024u * 1024u, "repeated metadata must share encoded strings");
		const auto Loaded = DecodeSourceMetadata(Bytes.data(), Bytes.size(), Error);
		Require(bool(Loaded), Error);
		Require(Loaded->Nodes.size() == Source.Nodes.size() && Loaded->Documents.size() == 2,
		    "metadata documents and nodes retained");
		Require(Loaded->Documents[1].Text == Active.Text && Loaded->Documents[1].Mappings[0].Origin.Range.SourceId == Raw.Id,
		    "source text and mapping retained");
		Require(Loaded->Nodes[1].Comments.size() == 1 && Loaded->Nodes[1].Comments[0].Text == Comment.Text &&
		        Loaded->Nodes[1].Comments[0].OwnerId == Comment.OwnerId &&
		        Loaded->Definitions.size() == 1 && Loaded->Definitions[0].Name == Definition.Name,
		    "source comments and annotation definitions retained");
		for (std::size_t Index = 1; Index < Source.Nodes.size(); ++Index)
		{
			const auto& Before = Source.Nodes[Index];
			const auto& After = Loaded->Nodes[Index];
			Require(After.Id == Before.Id && After.OwnerId == Root.Id && After.ParentId == Root.Id &&
			        After.ModuleId == Path && After.Range.SourceId == Path && After.Range.Begin == 4 && After.Range.End == 6 &&
			        After.Range.Position.Begin.FileLabel == Path && After.Range.Position.Begin.Line == 2 &&
			        After.Range.Position.End.Column == 3 && After.Origin.Range.SourceId == Path &&
			        After.Origin.GenerationId == "generation" && After.Annotations.size() == 1 &&
			        After.Annotations[0].DefinitionId == Definition.Id && After.Annotations[0].NamePosition.FileLabel == Path,
			    "metadata identity, positions, annotation and origin retained");
		}
		std::vector<std::uint8_t> Again;
		Require(EncodeSourceMetadata(*Loaded, Again, Error) && Bytes == Again, "deterministic metadata encoding");
		// A reference cannot precede its literal in this snapshot's dictionary.
		for (std::size_t Index = 0; Index < 8; ++Index)
		{
			Bytes[24 + Index] = Index == 7 ? 0x80 : 0;
		}
		Require(!DecodeSourceMetadata(Bytes.data(), Bytes.size(), Error) &&
		        Error == "invalid source metadata string reference",
		    "reject an unresolved string reference");
		std::cout << "repeated source metadata bytes=" << Again.size() << '\n';
	}
	void TestLiteralSourceMetadata()
	{
		std::vector<std::uint8_t> Bytes;
		const auto U = [&](std::uint64_t Value)
		{
			Bytecode::WriteU64LE(Bytes, Value);
		};
		const auto Text = [&](const std::string& Value)
		{
			U(Value.size());
			Bytes.insert(Bytes.end(), Value.begin(), Value.end());
		};
		U(0x3354454d43534256ull);
		U(0);
		U(1);
		Text("legacy");
		Text("legacy");
		Text("x");
		U(1);
		U(0);
		U(0);
		U(0);
		U(0);
		U(0);
		std::string Error;
		const auto Source = DecodeSourceMetadata(Bytes.data(), Bytes.size(), Error);
		Require(Source && Source->Documents.size() == 1 && Source->Documents[0].Text == "x", "read literal metadata: " + Error);
	}
	void TestSourceMetadataByteBudget()
	{
		FSourceMetadataSnapshot Source;
		FSourceDocument Document;
		Document.Id = Document.ModuleId = "large";
		Document.Text.assign(64u * 1024u * 1024u, 'x');
		Source.Documents.push_back(std::move(Document));
		std::vector<std::uint8_t> Bytes;
		std::string Error;
		Require(!EncodeSourceMetadata(Source, Bytes, Error) && Bytes.empty() && Error == "source metadata byte budget exceeded",
		    "source metadata byte limit must remain 64 MiB");
	}
}
int main()
{
	try
	{
		TestRepeatedSourceMetadata();
		TestLiteralSourceMetadata();
		TestSourceMetadataByteBudget();
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
	return 0;
}
