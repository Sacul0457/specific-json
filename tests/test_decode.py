import unittest
import specific_json

# Created by ChatGPT
class TestDecode(unittest.TestCase):
    def test_null(self):
        self.assertIsNone(specific_json.json_decode(b"null"))

    def test_booleans(self):
        self.assertIs(specific_json.json_decode(b"true"), True)
        self.assertIs(specific_json.json_decode(b"false"), False)

    def test_integers(self):
        cases = [
            (b"0", 0),
            (b"1", 1),
            (b"-1", -1),
            (b"123456789", 123456789),
            (b"9223372036854775807", 9223372036854775807),
            (b"-9223372036854775808", -9223372036854775808),
        ]

        for data, expected in cases:
            with self.subTest(data=data):
                self.assertEqual(specific_json.json_decode(data), expected)

    def test_floats(self):
        cases = [
            (b"0.0", 0.0),
            (b"1.5", 1.5),
            (b"-1.5", -1.5),
            (b"1e10", 1e10),
            (b"1.5e-3", 1.5e-3),
        ]

        for data, expected in cases:
            with self.subTest(data=data):
                self.assertEqual(specific_json.json_decode(data), expected)

    def test_strings(self):
        cases = [
            (b'""', ""),
            (b'"hello"', "hello"),
            (b'"hello world"', "hello world"),
        ]

        for data, expected in cases:
            with self.subTest(data=data):
                self.assertEqual(specific_json.json_decode(data), expected)

    def test_unicode(self):
        cases = [
            ('"こんにちは"', "こんにちは"),
            ('"Γ"', "Γ"),
            ('"❌"', "❌"),
            ('"😀"', "😀"),
        ]

        for data, expected in cases:
            with self.subTest(data=data):
                self.assertEqual(
                    specific_json.json_decode(data.encode("utf-8")),
                    expected,
                )

    def test_arrays(self):
        self.assertEqual(
            specific_json.json_decode(b"[1, 2, 3]"),
            [1, 2, 3],
        )

        self.assertEqual(
            specific_json.json_decode(b"[]"),
            [],
        )

    def test_objects(self):
        self.assertEqual(
            specific_json.json_decode(b'{"a": 1, "b": 2}'),
            {"a": 1, "b": 2},
        )

        self.assertEqual(
            specific_json.json_decode(b"{}"),
            {},
        )

    def test_nested_structures(self):
        data = b"""
        {
            "name": "test",
            "items": [1, 2, {"value": true}],
            "metadata": {
                "enabled": true,
                "description": null
            }
        }
        """

        expected = {
            "name": "test",
            "items": [1, 2, {"value": True}],
            "metadata": {
                "enabled": True,
                "description": None,
            },
        }

        self.assertEqual(specific_json.json_decode(data), expected)

    def test_whitespace(self):
        cases = [
            b"  123  ",
            b"\ntrue\n",
            b"\t[1, 2, 3]\t",
            b'{ "a" : 1 }',
        ]

        expected = [123, True, [1, 2, 3], {"a": 1}]

        for data, value in zip(cases, expected):
            with self.subTest(data=data):
                self.assertEqual(specific_json.json_decode(data), value)


if __name__ == "__main__":
    unittest.main()
