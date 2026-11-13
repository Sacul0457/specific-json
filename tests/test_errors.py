import unittest
import specific_json

# Created by ChatGPT
class TestInvalidJSON(unittest.TestCase):
    def assert_invalid(self, data):
        with self.subTest(data=data):
            with self.assertRaises(ValueError):
                specific_json.json_decode(data)

    def test_unterminated_strings(self):
        for data in (
            b'"',
            b'"hello',
        ):
            self.assert_invalid(data)

    def test_invalid_separators(self):
        for data in (
            b"[1 2]",
            b"[1,]",
            b"[,1]",
            b'{"a": 1 "b": 2}',
            b'{"a": 1,}',
            b'{,"a": 1}',
        ):
            self.assert_invalid(data)

    def test_invalid_literals(self):
        for data in (
            b"True",
            b"False",
            b"None",
            b"nul",
            b"tru",
            b"fals",
            b"undefined",
        ):
            self.assert_invalid(data)

    def test_invalid_numbers(self):
        for data in (
            b"+1",
            b"01",
            b"-01",
            b".1",
            b"1.",
            b"1e",
            b"1e+",
            b"--1",
            b"-",
        ):
            self.assert_invalid(data)

    def test_trailing_data(self):
        for data in (
            b"null false",
            b"123abc",
            b"true{}",
            b"[]{}",
        ):
            self.assert_invalid(data)

    def test_invalid_control_characters(self):
        for char in range(0x20):
            data = b'"hello' + bytes([char]) + b'world"'
            self.assert_invalid(data)


if __name__ == "__main__":
    unittest.main(verbosity=2)
