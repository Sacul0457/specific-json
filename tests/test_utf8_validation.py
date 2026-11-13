import unittest
import specific_json

# Created by ChatGPT
class TestUTF8(unittest.TestCase):
    def test_valid_utf8(self):
        cases = [
            ("é", "é"),
            ("Γ", "Γ"),
            ("你好", "你好"),
            ("❌", "❌"),
            ("😀", "😀"),
            ("𐍈", "𐍈"),
        ]

        for source, expected in cases:
            with self.subTest(source=source):
                self.assertEqual(
                    specific_json.json_decode(
                        b'"' + source.encode("utf-8") + b'"'
                    ),
                    expected,
                )

    def test_invalid_utf8(self):
        cases = [
            b'"\x80"',                 # Invalid continuation byte
            b'"\xC0\x80"',             # Overlong encoding
            b'"\xC1\xBF"',             # Invalid leading byte
            b'"\xE2\x28\xA1"',         # Invalid continuation byte
            b'"\xE2\x82"',             # Truncated 3-byte sequence
            b'"\xF0\x9F\x98"',         # Truncated 4-byte sequence
            b'"\xF5\x80\x80\x80"',     # Above U+10FFFF
            b'"\xED\xA0\x80"',         # UTF-8 encoded surrogate
            b'"\xF0\x80\x80\x80"',     # Overlong encoding
        ]

        for data in cases:
            with self.subTest(data=data):
                with self.assertRaises(UnicodeDecodeError):
                    specific_json.json_decode(data)

    def test_ascii_and_non_ascii_mixed(self):
        data = '{"hello": "world ❌", "other": "こんにちは"}'.encode()

        self.assertEqual(
            specific_json.json_decode(data),
            {
                "hello": "world ❌",
                "other": "こんにちは",
            },
        )


if __name__ == "__main__":
    unittest.main(verbosity=3)
