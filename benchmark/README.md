# SB-Bench: ShimbaBomb Coding Benchmark

A HumanEval-style benchmark for the **ShimbaBomb (SB)** language — an English-like scripting language that compiles to native C.

## Language Reference

SB files start with `SB`. Every statement ends with `.` (period).

### Variables
```sb
set x to 10.
set name to "alice".
set flag to true.
```

### Functions
```sb
define add with a and b as
    give back a plus b.
end
```

### Conditionals
```sb
if x is greater than 5 then
    say "big".
otherwise
    say "small".
end
```

### Loops
```sb
count i from 1 to 10 then
    say i.
end

loop item through my_list then
    say item.
end

while x is less than 100 then
    set x to x times 2.
end
```

### Lists
```sb
set xs to list with 1 and 2 and 3.
add_to with xs and 4.
say length with xs.
say item with 0 and xs.
set xs[0] to 99.
```

### Strings
```sb
say length with "hello".
say upper with "abc".
say lower with "ABC".
say split with "a b c" and " ".
say join with list with "a" and "b" and " " and "-".
say s[i].
```

### Classes
```sb
class Dog with name and age
    method bark as
        say self's name plus " woofs".
    end
end
set rex to new Dog with "Rex" and 3.
rex's bark.
```

### Operators
- Arithmetic: `plus`, `minus`, `times`, `divided by`, `divided evenly by`, `mod` (symbols `+ - * /` also work)
- Comparison: `is`, `is not`, `is less than`, `is greater than`, `is equal to` (symbols `< > <= >= == !=` also work; `=` means equal)
- Logical: `and`, `or`, `not` — `not` negates one comparison (`not a and b` is `(not a) and b`); use parens to group (`(a or b) and c`)
- String concat: `plus`
- Indexing: `s[i]`, `xs[i]`, `m["key"]`
- Possessive: `obj's field`
- Notes: arithmetic is not allowed on the left of a comparison — bind a temp first (`set right to x plus w.` then `if right is greater than 640`). Nested `with` calls only work as the last argument — bind temps first.

### Numbers and math
```sb
say sin with 0.
say cos with 0.
say sqrt with 25.
say pi with.
```

### Errors (coded, never match message text)
```sb
set id to sprite_load with "/no/such.ppm".
if is_error with id then
    say err_name with err_code with id.
end
try
    raise "boom".
catch err
    say err.
    say err_info["code"].
end
```
Error names: `none`, `type_mismatch`, `div_zero`, `out_of_bounds`, `not_found`, `bad_args`, `gui`, `io`, `audio`, `runtime`.
`catch err` binds the message string to `err` plus a structured `err_info` map (`code`, `message`, `line`, `trace`).

### Time (frame clocks, fixed-timestep loops)
```sb
set t0 to time_ms with.
sleep_ms with 16.
say time_ms with minus t0.
```

### Game helpers (`pls bring game.`)
Collision (`hit_box`, `hit_circle`, `hit_point` give back 1/0), `frame_sleep with t0 and budget`, `fps_of with frames and ms`, overlapping sound via `sfx with file` (needs `paplay`/`aplay`).

### 3D wireframe (`pls bring ketiwe3d.`)
Points are maps with `x`/`y`/`z`: `v3`, `v3_add`, `rot_x`/`rot_y`/`rot_z` (radians), perspective `project`, `cube_verts`, `cube_edges`, `draw_wire` plus filled `draw_solid`, `sphere`, `grid_lines`.

### Other
```sb
# comment
say "hello".
raise "error message".
try ... catch e ... end
```

## Dataset Format

`sb_benchmark.jsonl` — one JSON object per line:

```json
{
  "task_id": "sb_001",
  "difficulty": "easy|medium|hard",
  "category": "functions|loops|recursion|lists|strings|classes|conditionals",
  "prompt": "Description of what to implement",
  "entry_point": "function_or_class_name",
  "test": "SB assert statements to validate the solution",
  "solution": "reference solution in SB"
}
```

## Tasks

| ID | Difficulty | Category | Description |
|------|------------|----------|-------------|
| sb_001 | easy | functions | Square a number |
| sb_002 | easy | functions | Add two numbers |
| sb_003 | easy | conditionals | Absolute value |
| sb_004 | easy | conditionals | Max of two numbers |
| sb_005 | easy | loops | Sum 1 to n |
| sb_006 | easy | loops | Factorial |
| sb_007 | easy | strings | Repeat string |
| sb_008 | medium | recursion | Fibonacci |
| sb_009 | medium | recursion | Power function |
| sb_010 | medium | lists | Find max in list |
| sb_011 | medium | lists | Reverse a list |
| sb_012 | medium | lists | Count even numbers |
| sb_013 | medium | strings | Count character occurrences |
| sb_014 | medium | strings | Palindrome check |
| sb_015 | medium | loops | GCD (Euclidean) |
| sb_016 | hard | recursion | Tower of Hanoi moves |
| sb_017 | hard | lists | Bubble sort |
| sb_018 | hard | lists | Flatten nested lists |
| sb_019 | hard | strings | Reverse a string |
| sb_020 | hard | recursion | Count subsets |
| sb_021 | easy | variables | Celsius to Fahrenheit |
| sb_022 | medium | lists | Zip-sum two lists |
| sb_023 | medium | conditionals | FizzBuzz |
| sb_024 | hard | lists | Merge sort |
| sb_025 | easy | functions | Is even |
| sb_026 | medium | strings | Capitalize words |
| sb_027 | hard | recursion | Permutations (n!) |
| sb_028 | hard | lists | Remove duplicates |
| sb_029 | easy | loops | Product of list |
| sb_030 | medium | classes | Rectangle class |

## Usage with HuggingFace

```python
from datasets import load_dataset

ds = load_dataset("json", data_files="sb_benchmark.jsonl")

# filter by difficulty
easy = ds["train"].filter(lambda x: x["difficulty"] == "easy")

# get prompt + test
for row in easy:
    print(row["prompt"])
    print(row["test"])
```

## Evaluation

A generated solution passes a task if all `assert` statements in the `test` field run without errors when the `solution` code is prepended.

## License

MIT
