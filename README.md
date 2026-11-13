# Specific-json
A python JSON serialisation library implemented as a C extension. 
- **Speed:** Initial benchmarks show *non-float/exponential* json data to be **on par** with `orjson` and `msgspec`.
- **Memory:** Initial benchmarks show memory usage close or sometimes less than `msgspec`.

See more at [Benchmarks](#Benchmarks).
> [!IMPORTANT]
> This project is under development and not meant to be used in prod. (if ever haha)

## Features
- Extremely fast implementations
- Very low memory usage
- Allows for python like indexing, hence avoiding creation of intermediate/unused python objects.

## Benchmarks

### [Msgspec Benchmarks](https://github.com/msgspec/msgspec/blob/main/benchmarks/bench_large_json.py)
| Decoder              | decode (μs) |  vs.      |
|----------------------|:-----------:|:---------:|
| msgspec structs      | 332.3       | 1.00x     |
| msgspec              | 433.9       | 1.31x     |
| specific-json        | 436.7       | 1.31x     |
| orjson               | 528.5       | 1.59x     |
| json                 | 817.4       | 2.46x     |

### Orjson Benchmarks (`citm_catalog.json` Fixture)
| Decoder              | decode (ms) |  vs.      |
|----------------------|:-----------:|:---------:|
| orjson               | 2.376       | 1.00x     |
| specific-json        | 2.550       | 1.07x     |
| msgspec              | 2.718       | 1.14x     |
| json                 | 6.795       | 2.86x     |

> [!IMPORTANT]
> Regarding the orjson benchmarks, since `specific-json` cannot handle escaped characters yet, the data for all libs had no escaped chars. 
> 
> BUT, only one string had escaped characters, so overall performance should not be heavily affected.

Do take these benchmarks with a pinch of salt, these are results on my machine (Windows) and they have been modified to fit in one file for convenience.
A more reliable benchmark will be used and uploaded in the future hopefully.

## Installing
**All major platforms** are supported on [Pypi](https://pypi.org/manage/project/specific-json/releases/)!

> [!NOTE]
> Python 3.14 is required (for now)

```sh
#windows
py -3.14 -m pip install -U specific-json

#linux/macos
python3.14 -m pip install -U specific-json
```

## Compiling
If you want to build this yourself, run this command to compile the extension in the root directory.
```
py -3.14 setup.py build_ext --inplace
```

## Example
```python
from specific_json import json_decode

json_str = \
"""
{
  "name": "Sacul",
  "active": true,
  "tags": ["python", "c", "json"]
}
"""

def main():
    whole_obj = json_decode(json_str)

    is_active = json_decode(json_str, "active")
    first_tag = json_decode(json_str, "tags", 0)

    print(whole_obj)
    print(is_active)
    print(first_tag)

if __name__ == "__main__":
    main()
```
## Development
The project is currently incomplete, see the [TODO](https://github.com/Sacul0457/specific-json/issues/1).
