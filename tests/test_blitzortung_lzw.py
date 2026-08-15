#!/usr/bin/env python3
"""Reference check for the browser-compatible Blitzortung LZW stream."""

SAMPLE = ('{"time":1786808428328945400,"lat":43.060369,"lon":0.159398,'
          '"alt":0,"pol":0,"mds":6093,"mcg":248,"status":2,"region":9,'
          '"sig":[{"sta":3064,"time":3363592,"lat":51.232872328,'
          '"lon":6.25232826,"alt":55,"status":4}]}')


def encode(text: str) -> str:
    dictionary = {}
    phrase = text[0]
    code = 256
    output = []
    for char in text[1:]:
        pair = phrase + char
        if pair in dictionary:
            phrase = pair
        else:
            output.append(dictionary[phrase] if len(phrase) > 1 else ord(phrase))
            dictionary[pair] = code
            code += 1
            phrase = char
    output.append(dictionary[phrase] if len(phrase) > 1 else ord(phrase))
    return ''.join(chr(item) for item in output)


def decode(text: str) -> str:
    codes = [ord(char) for char in text]
    prefix = [0] * 16384
    suffix = [0] * 16384
    first = [0] * 16384

    output = chr(codes[0])
    previous_code = codes[0]
    previous_first = codes[0]
    next_code = 256

    def expand(code: int, limit: int):
        if code < 256:
            return chr(code), code
        stack = []
        cursor = code
        while cursor >= 256:
            assert cursor < limit
            stack.append(suffix[cursor])
            cursor = prefix[cursor]
        stack.append(cursor)
        return ''.join(chr(x) for x in reversed(stack)), stack[-1]

    for code in codes[1:]:
        assert code <= next_code
        if code < 256:
            current_first = code
        elif code < next_code:
            current_first = first[code]
        else:
            current_first = previous_first

        prefix[next_code] = previous_code
        suffix[next_code] = current_first
        first[next_code] = previous_first
        next_code += 1

        expanded, _ = expand(code, next_code)
        output += expanded
        previous_code = code
        previous_first = current_first

    return output

compressed = encode(SAMPLE)
assert decode(compressed) == SAMPLE
assert len(compressed.encode('utf-8')) > len(compressed)  # dictionary codes become UTF-8
assert '"lat":43.060369' in decode(compressed)
assert '"lon":0.159398' in decode(compressed)
print('BLITZORTUNG LZW TEST OK: browser-style LZW roundtrip and UTF-8 code points')
