#include "MCPSearchPatterns.h"
#include "Internationalization/Regex.h"

bool FMCPSearchPatterns::Matches(const FString& Pattern, const FString& Value)
{
    if (Pattern.IsEmpty())
    {
        return true;
    }

    FString RegexStr;

    if (Pattern.StartsWith(TEXT("/")))
    {
        // Raw regex mode: strip leading '/' and optional trailing '/'
        RegexStr = Pattern.Mid(1);
        if (RegexStr.EndsWith(TEXT("/")))
        {
            RegexStr = RegexStr.LeftChop(1);
        }
    }
    else
    {
        // Comma OR: split, convert each segment via wildcard-to-regex, join with '|'
        TArray<FString> Segments;
        Pattern.ParseIntoArray(Segments, TEXT(","), true);

        TArray<FString> RegexSegments;
        for (FString Segment : Segments)
        {
            RegexSegments.Add(WildcardToRegex(Segment.TrimStartAndEnd()));
        }

        RegexStr = FString::Join(RegexSegments, TEXT("|"));
    }

    FRegexPattern CompiledPattern(RegexStr, ERegexPatternFlags::CaseInsensitive);
    FRegexMatcher Matcher(CompiledPattern, Value);
    return Matcher.FindNext();
}

FString FMCPSearchPatterns::WildcardToRegex(const FString& Wildcard)
{
    FString Result;
    Result.Reserve(Wildcard.Len() * 2);

    for (const TCHAR Ch : Wildcard)
    {
        switch (Ch)
        {
        case TEXT('\\'):
        case TEXT('^'):
        case TEXT('$'):
        case TEXT('.'):
        case TEXT('+'):
        case TEXT('('):
        case TEXT(')'):
        case TEXT('['):
        case TEXT(']'):
        case TEXT('{'):
        case TEXT('}'):
            Result += TEXT('\\');
            Result += Ch;
            break;
        case TEXT('*'):
            Result += TEXT(".*");
            break;
        case TEXT('?'):
            Result += TEXT('.');
            break;
        default:
            // '|' and all other chars pass through unchanged
            Result += Ch;
            break;
        }
    }

    return Result;
}

TArray<FString> FMCPSearchPatterns::FilterStrings(const TArray<FString>& Values, const FString& Pattern)
{
    TArray<FString> Result;
    for (const FString& Value : Values)
    {
        if (Matches(Pattern, Value))
        {
            Result.Add(Value);
        }
    }
    return Result;
}
