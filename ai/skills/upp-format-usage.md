# Upp::Format Usage Guide

## Purpose

This skill provides comprehensive guidance for properly using `Upp::Format()` function. Upp::Format is U++'s built-in formatting function that differs significantly from C standard library functions like `fprintf`, `sprintf`, `printf_s`, etc.

## Key Differences from fprintf/sprintf

| Feature | fprintf/sprintf | Upp::Format |
|---------|-----------------|-------------|
| Template syntax | `%d`, `%s`, `%f` | Similar but extended |
| Positional args | Manual reordering needed | `%1$d`, `%2$s` (reverse order) |
| Width control | `%10s` (right-aligned) | `%20<d` (left), `%20>d` (right), `%20=d` (center) |
| Zero padding | `%05d` | `%05d` |
| Empty value handling | Manual check required | `%[empty]~d` |
| Date/time | Manual `strftime` | Built-in tokens |
| Token count | Standard | Extended |

## Structure

Upp::Format uses a template string with tokens:
- **Conversion specifier**: Starts with `%`, ends with a type letter (`d`, `s`, `f`, `e`, `m`, `c`, etc.)
- **Flags**: Control filling, alignment, prefixes
- **Width**: Field width specification
- **Precision**: Decimal places or character count
- **Positional**: Argument reordering with `n:` prefix

All formatting happens in a single token: `<flag><width>.<precision><pos><type>`

## Core Formatting Types

### Integer Formats

```cpp
Format("%d", 123)        // 123
Format("%i", 123)        // 123 (same as %d)
Format("%o", 123)        // 173 (octal)
Format("%x", 123)        // 7b (hex lowercase)
Format("%X", 123)        // 7B (hex uppercase)
```

### Float/Double Formats

```cpp
Format("%f", 1234567.89)     // 1234567.890000
Format("%.5f", 1234567.89)  // 1234567.89000
Format("%e", 1234567.89)     // 1.234568e+006
Format("%E", 1234567.89)     // 1.234568E+006
Format("%g", 1234567.89)     // 1.23457e+06 (compact)
Format("%G", 1234567.89)     // 1.23457E+06 (compact uppercase)
Format("%m", 1234567.89)     // 1.23457e+06 (short e)
Format("%M", 1234567.89)     // 1.23457E+06 (short E)
```

### String Formats

```cpp
Format("%s", "TEXT")       // TEXT
Format("%c", 65)         // A (character from ASCII)
```

## Position Reordering

### Reverse Order (dth example seems to reference this)

```cpp
Format("%d, %s", 123, "TEXT")     // 123, TEXT
Format("%2:s, %1:d", 123, "TEXT") // TEXT, 123
```

Use positional arguments (`%1$-`, `%2$s` etc.) when calling code doesn't guarantee argument order.

## Width and Alignment

### Standard Right-Aligned (default)

```cpp
Format("%10d", 123)   //      123
```

### Left-Aligned with `<

```cpp
Format("%20<d|", 123)   // |123                 |
```

### Right-Aligned with `>`

```cpp
Format("%20>d|", 123)  // |                 123|
```

### Center-Aligned with `=`

```cpp
Format("%20=d|", 123)  // |        123         |
```

### Zero Padding

```cpp
Format("%010d", 123)      // 0000000123
Format("%0*d", 11, 123)   // 00000000123
```

Variable width:

```cpp
int width = 15;
Format("%0*d", width, 123)  // 000000000001234 (15 zeros)
```

## Precision Control

### Float Precision

```cpp
Format("%.10m", 1234567.89)  // 1234567.89 (force 10 decimal places)
Format("%.10!m", 1234567.89) // 1234567.890 (always show trailing zeros)
Format("%.6m", 1234567.89)   // 1.23457e+06
Format("%.7m", 1234567.89)   // 1234568 (rounded to integer)
```

## Special Values

### Units

```cpp
Format("%dpt", 123)   // 123pt
Format("%dm", 123)    // 123m
```

### Empty/Null Values

```cpp
Format("%[empty]~d", 123)   // 123
Format("%[empty]~d", Null)  // empty
Format("%[empty]~s", Null)  // empty
```

### NaN Handling

```cpp
double nan_val = /* some operation that gives NaN */;
Format("%m", nan_val)   // -nan
Format("%?m", nan_val)  // 0 (converts NaN to 0)
Format("%_m", -0.0)     // 0 (converts -0 to 0)
```

### Large Numbers Formatting

```cpp
Format("%m", 1e9)       // 1e+09
Format("%^m", 1e9)      // 1e09 (compact)
Format("%&m", 1e9)      // 1e+9 (short sign)
Format("%&^m", 1e9)     // 1e9 (short compact)
```

## Number Grouping and Signs

### Comma Separator

```cpp
Format("%,m", 1234567.89)  // 1,23457e+06
```

### Explicit Sign

```cpp
Format("%+m", 1234567.89)  // +1.23457e+06
Format("% m", 1234567.89)  //  1.23457e+06 (space for positive)
```

### Percent Sign Separator

```cpp
Format("%", 123)   // 123 (just prints the value, no extra char)
```

## Date/Time Formatting

### Month Names

```cpp
Format("%month", 6)   // june (lowercase)
Format("%Month", 6)   // June (title case)
Format("%MONTH", 6)   // JUNE (uppercase)
```

### Day Names

```cpp
Format("%day", 6)     // saturday (lowercase)
Format("%Day", 6)     // Saturday (title case)
Format("%DAY", 6)     // SATURDAY (uppercase)
Format("%dy", 6)      // sa (abbreviation)
Format("%Dy", 6)      // Sa (title case abbreviation)
Format("%DY", 6)      // SA (uppercase abbreviation)
```

### Week Number

```cpp
Format("%tw", 0)   // 12 (first week)
Format("%tw", 5)   // 5 (fifth week)
Format("%tw", 15)  // 3 (three-month period = 3 weeks)
Format("%0tw", 15) // 03 (zero-padded)
```

### Ordinal Day

```cpp
Format("%a", 1)    // A (ordinal A)
Format("%a", 123)  // es (ordinal ES)
Format("%A", 1)    // A (uppercase)
Format("%A", 123)  // ES (uppercase ordinal)
Format("%r", 8)    // viii (Roman numerals)
Format("%R", 1231) // MCCXXXI (Roman numerals)
```

### Full Date/Time

```cpp
Format("%", GetSysDate())   // 11/11/2011
Format("%", GetSysTime())   // 11/11/2011 14:44:11

// Or use String format
SysDate(), SysTime().ToString()
```

## Conditional and Enum Formatting

### String Selection (Enum-like)

```cpp
Format("%[1:one;2:two;3:three;another]s", 2)  // two
Format("%[1:one;2:two;3:three;another]s", 20) // another (default)
Format("%[3%1:one;2:two;3:three;another]s", 20) // two (use %n for value)
```

Format as:
- `%n:1;2;3;default)s` - numeric selection
- `%value:key1;key2;default)s` - value-based selection

## Scientific Notation Variants

### Exponent Formatting

```cpp
Format("%.5mf", 1234567.89)  // 1234567.89000
Format("%.2me", 1234567.89)  // 1.23e+06
Format("%.2mE", 1234567.89)  // 1.23E+06
```

## Common Patterns

### Align Right in Fixed Width

```cpp
Format("%10d", 123)      // Right-aligned in 10 chars
Format("%s", 123)        // Just prints the value
```

### Pad with Spaces

```cpp
string padded = Format("%20>s", "text");  // Right-padded to 20 chars
string padded = Format("%20<s", "text");  // Left-padded to 20 chars
```

### Currency/Unit Prefix

```cpp
Format("%d €", 123)      // 123 €
Format("%d%%", 85)       // 85%
```

### Progress/Status

```cpp
Format("Progress: %d%%", 75)   // Progress: 75%
Format("Size: %d bytes", 1024) // Size: 1024 bytes
```

## Common Mistakes

### 1. Forgetting Backslash Escape

```cpp
// WRONG
Format("%d%", 123)   // Ambiguity

// RIGHT
Format("%d%%", 123)  // 123%
```

### 2. Wrong Positional Syntax

```cpp
// WRONG: No type specifier
Format("%1$, 123)

// RIGHT: Complete specifier
Format("%1$d", 123)
```

### 3. Mismatched Width/Alignment

```cpp
// WRONG: Confusion about padding
Format("%10<s", "text")  // text        (left-padded)

// Usually want:
Format("%10>s", "text")  //        text (right-padded)
```

## Integration with Other Skills

### With Build U++ Package Skill

Log build results with proper formatting:

```cpp
LOG(Format("Build %s: %dms", packageName, buildTimeMs));
LOG(Format("Warnings: %[empty]~d", warningCount));
LOG(Format("Errors: %d", errorCount));
```

### With Multi-Agent Coordination

Format planner messages:

```cpp
string msg = Format("Agent %s: %s", agentId, actionName);
LOG(msg);
```

### With GUI Skills

Status labels and reports:

```cpp
label.SetText(Format("Items: %d/%d", current, total));
statusBar.SetText(Format("Progress: %.1f%%", progress));
```

### With TOON Files

Include formatted data:

```cpp
// In a .tff file structure
BuildReport
    Package = Eon01
    Duration = %dms
    Status = %s
```

## Quick Reference Table

| Format | Result | Notes |
|--------|--------|-------|
| `%s` | String as-is | Default for strings |
| `%d` | Integer | Decimal |
| `%i` | Integer | Same as %d |
| `%f` | Float | Fixed decimal |
| `%e` | Float | Scientific notation |
| `%g` | Float | Compact, %f or %e |
| `%c` | Character | ASCII code |
| `%m` | Float | Short scientific |
| `%r` | Integer | Roman numerals |
| `%month` | String | Full month name |
| `%week` | Integer | Week of year |
| `%[cond]~f` | Conditional | Shows value or "empty" |
| `%n>type` | Positional | Nth argument |

## Checklist

- [ ] Understand token structure: `<flags><width>.<precision><pos><type>`
- [ ] Know difference between `<` `>` `=` for alignment
- [ ] Use `%[empty]~` for Null handling
- [ ] Use positional args `%1$d` for complex formatting
- [ ] Test with edge cases (Null, 0, negative, very large)
- [ ] Remember that `%s` prints the entire value even as format string itself
- [ ] Use correct specifier when reordering arguments