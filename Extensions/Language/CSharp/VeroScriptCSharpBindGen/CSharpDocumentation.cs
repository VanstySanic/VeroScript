// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace VeroScript.CSharpBindGen;

internal sealed record CSharpDocumentation(
	string Summary,
	IReadOnlyDictionary<string, string> Parameters,
	string Returns)
{
	private static readonly Regex Whitespace = new(@"\s+", RegexOptions.Compiled);

	internal static CSharpDocumentation Parse(string xml)
	{
		if (string.IsNullOrWhiteSpace(xml)) return new("", new Dictionary<string, string>(), "");
		try
		{
			XElement root = XElement.Parse(xml, LoadOptions.PreserveWhitespace);
			string summary = Normalize(root.Element("summary")?.Value);
			string returns = Normalize(root.Element("returns")?.Value);
			var parameters = root.Elements("param")
				.Where(element => element.Attribute("name") is not null)
				.GroupBy(element => element.Attribute("name")!.Value, StringComparer.Ordinal)
				.ToDictionary(group => group.Key, group => Normalize(group.First().Value), StringComparer.Ordinal);
			return new(summary, parameters, returns);
		}
		catch (System.Xml.XmlException)
		{
			return new(Normalize(xml), new Dictionary<string, string>(), "");
		}
	}

	private static string Normalize(string? text)
		=> string.IsNullOrWhiteSpace(text) ? "" : Whitespace.Replace(text, " ").Trim();
}
