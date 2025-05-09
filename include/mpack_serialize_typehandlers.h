#ifndef MPACK_SERIALIZE_TYPEHANDLERS_H
#define MPACK_SERIALIZE_TYPEHANDLERS_H

#include <iostream>
#include <string>
#include <array>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#include <optional>
#include <map>
#include <cstdint>
#include "mpack/mpack.h"

template<size_t N>
struct MsgPackExtension
{
  int8_t type;
  char buffer[N];
  static constexpr size_t size=N;

  MsgPackExtension() : MsgPackExtension(0) {}
  MsgPackExtension(int8_t t) : type(t) {
    std::fill(std::begin(buffer), std::end(buffer), 0);
  }
};



// Serialization framework
namespace serialization
{

// Helper to detect if a type can be treated as a string
template<typename T>
struct is_string_like : std::false_type {};

template<>
struct is_string_like<std::string>: std::true_type {};

template<>
struct is_string_like<const char *>: std::true_type {};

template<size_t N>
struct is_string_like<char[N]>: std::true_type {};


// Helper to detect if a type inherits from Serializable
template<typename T>
struct is_serializable
{
private:
  template<typename U>
  static auto test(U * u) -> decltype(u->serialize(nullptr), std::true_type{});

  template<typename>
  static std::false_type test(...);

public:
  static constexpr bool value = decltype(test<T>(nullptr))::value;
};

template<typename T>
inline constexpr bool is_serializable_v = is_serializable<T>::value;

// Type tag system for serialization
enum class TypeTag
{
  Missing,
  Nil,
  Bool,
  Integer,
  UInt,
  Float,
  Double,
  String,
  Binary,
  Array,
  Map,
  CustomObject
};

// Default handler for primitive types
template<typename T, typename = void>
struct TypeHandler
{
  static constexpr TypeTag tag =
    std::is_integral_v<T> ? TypeTag::Integer :
    std::is_floating_point_v<T> ? TypeTag::Float :
    is_string_like<T>::value ? TypeTag::String :
    TypeTag::CustomObject;


  static void write(mpack_writer_t * writer, const T & value)
  {
    if constexpr (std::is_integral_v<T>) {
      mpack_write_int(writer, static_cast<int64_t>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
      mpack_write_float(writer, static_cast<float>(value));
    } else if constexpr (is_string_like<T>::value) {
      if constexpr (std::is_same_v<T, std::string>) {
        mpack_write_cstr(writer, value.c_str());
      } else {
        mpack_write_cstr(writer, value);
      }
    } else if constexpr (is_serializable_v<T>) {
      value.serialize(writer);
    } else {
      static_assert(
        sizeof(T) == 0,
        "Type is not serializable and does not match any known type");
    }
  }

  static void read(mpack_reader_t * reader, T & value)
  {
    if constexpr (std::is_integral_v<T>) {
      value = static_cast<T>(mpack_expect_int(reader));
    } else if constexpr (std::is_floating_point_v<T>) {
      value = static_cast<T>(mpack_expect_float(reader));
    } else if constexpr (std::is_same_v<T, std::string>) {
      mpack_tag_t tag = mpack_peek_tag(reader);
      if (tag.type != mpack_type_str) {
        throw std::runtime_error("Expected string type");
      }

      // Determine string length
      const size_t str_len = tag.v.l;
      value.resize(str_len); // ATTENTION: This may cause reallocation

      // Read directly into the string's buffer
      if (str_len > 0) {
        mpack_read_tag(reader); // Consume the tag we peeked
        mpack_read_bytes(reader, &value[0], str_len);
      } else {
        mpack_discard(reader);
        value.clear();
      }
    } else if constexpr (is_serializable_v<T>) {
      value.deserialize(reader);
    } else {
      static_assert(
        sizeof(T) == 0,
        "Type is not deserializable and does not match any known type");
    }
  }
};

// Variadic template version
template<typename ... Types>
struct TypeHandler<std::variant<Types...>>
{
  static void write(mpack_writer_t * writer, const std::variant<Types...> & value)
  {
    // Directly write the value
    std::visit(
      [writer](const auto & v) {
        using ValueType = std::decay_t<decltype(v)>;
        TypeHandler<ValueType>::write(writer, v);
      }, value);
  }

  static void read(mpack_reader_t * reader, std::variant<Types...> & value)
  {
    // Peek at the next tag
    mpack_tag_t tag = mpack_peek_tag(reader);

    // Try to match with a type in the variant
    bool matched = try_read_variant<0, Types...>(reader, value, tag);

    if (!matched) {
      throw std::runtime_error("Could not match any variant type with the MessagePack tag");
    }
  }

private:
  // Recursive template to try reading each variant type
  template<size_t I, typename First, typename ... Rest>
  static bool try_read_variant(
    mpack_reader_t * reader, std::variant<Types...> & value,
    const mpack_tag_t & tag)
  {
    if (can_read_as<First>(tag)) {
      First val;
      TypeHandler<First>::read(reader, val);
      value = std::move(val);
      return true;
    }

    if constexpr (sizeof...(Rest) > 0) {
      return try_read_variant<I + 1, Rest...>(reader, value, tag);
    }

    return false;
  }

  // Base case for the recursion
  template<size_t I>
  static bool try_read_variant(
    mpack_reader_t * reader, std::variant<Types...> & value,
    const mpack_tag_t & tag)
  {
    return false;
  }

  // Type matcher helper
  template<typename T>
  static bool can_read_as(const mpack_tag_t & tag)
  {
    if constexpr (std::is_same_v<T, bool>) {
      return tag.type == mpack_type_bool;
    } else if constexpr (std::is_integral_v<T>&& std::is_signed_v<T>) {
      return tag.type == mpack_type_int;
    } else if constexpr (std::is_integral_v<T>&& std::is_unsigned_v<T>) {
      return tag.type == mpack_type_uint;
    } else if constexpr (std::is_floating_point_v<T>) {
      return tag.type == mpack_type_float || tag.type == mpack_type_double;
    } else if constexpr (std::is_same_v<T, std::string>) {
      return tag.type == mpack_type_str;
    } else if constexpr (is_serializable_v<T>) {
      // For serializable types, we often use maps
      return tag.type == mpack_type_map;
    }
    // Add more type matchers as needed
    return false;
  }
};


// Specialization for bool
template<>
struct TypeHandler<bool>
{
  static constexpr TypeTag tag = TypeTag::Bool;

  static void write(mpack_writer_t * writer, bool value)
  {
    mpack_write_bool(writer, value);
  }

  static void read(mpack_reader_t * reader, bool & value)
  {
    value = mpack_expect_bool(reader);
  }
};

template<size_t N>
struct TypeHandler<MsgPackExtension<N>>
{
  static void write(mpack_writer_t * writer, const MsgPackExtension<N> & value)
  {
    mpack_write_ext(writer, value.type, value.buffer, N);
  }

  static void read(mpack_reader_t * reader, MsgPackExtension<N> & value)
  {
    int8_t returned_type;
    uint32_t buf_size = mpack_expect_ext(reader, &returned_type);
    if (buf_size > N) {
      throw std::runtime_error("Buffer size is too small");
    }
    mpack_read_bytes(reader, value.buffer, buf_size);
    value.type = returned_type;
  }
};

// Specialization for unsigned integers
template<typename T>
struct TypeHandler<T, std::enable_if_t<std::is_unsigned_v<T>&& !std::is_same_v<T, bool>>>
{
  static constexpr TypeTag tag = TypeTag::UInt;

  static void write(mpack_writer_t * writer, T value)
  {
    mpack_write_uint(writer, static_cast<uint64_t>(value));
  }

  static void read(mpack_reader_t * reader, T & value)
  {
    value = static_cast<T>(mpack_expect_u32(reader));
  }
};

// Specialization for double
template<>
struct TypeHandler<double>
{
  static constexpr TypeTag tag = TypeTag::Double;

  static void write(mpack_writer_t * writer, double value)
  {
    mpack_write_double(writer, value);
  }

  static void read(mpack_reader_t * reader, double & value)
  {
    value = mpack_expect_double(reader);
  }
};


// Specialization for std::optional
template<typename U>
struct TypeHandler<std::optional<U>>
{
  static constexpr TypeTag tag = TypeTag::Nil;

  static void write(mpack_writer_t * writer, const std::optional<U> & opt)
  {
    if (opt.has_value()) {
      TypeHandler<U>::write(writer, *opt);
    } else {
      mpack_write_nil(writer);
    }
  }

  static void read(mpack_reader_t * reader, std::optional<U> & opt)
  {
    mpack_tag_t tag = mpack_peek_tag(reader);
    if (tag.type == mpack_type_nil) {
      mpack_expect_nil(reader);
      opt = std::nullopt;
    } else {
      if (!opt.has_value()) {
        opt.emplace();
      }
      TypeHandler<U>::read(reader, *opt);
    }
  }
};

// Specialization for binary data (vector<char>)
template<>
struct TypeHandler<std::vector<char>>
{
  static constexpr TypeTag tag = TypeTag::Binary;

  static void write(mpack_writer_t * writer, const std::vector<char> & data)
  {
    mpack_write_bin(writer, data.data(), data.size());
  }

  static void read(mpack_reader_t * reader, std::vector<char> & result)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_bin) {
      throw std::runtime_error("Expected binary data");
    }
    result.resize(tag.v.n);
    mpack_expect_bin_size_buf(reader, result.data(), result.size());
  }
};

// Specialization for std::array
template<typename T, size_t N>
struct TypeHandler<std::array<T, N>>
{
  static constexpr TypeTag tag = TypeTag::Array;

  static void write(mpack_writer_t * writer, const std::array<T, N> & arr)
  {
    mpack_start_array(writer, N);
    for (const auto & item : arr) {
      TypeHandler<T>::write(writer, item);
    }
    mpack_finish_array(writer);
  }

  static void read(mpack_reader_t * reader, std::array<T, N> & result)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_array || tag.v.n != N) {
      throw std::runtime_error("Expected array of specific size");
    }

    for (size_t i = 0; i < N; ++i) {
      TypeHandler<T>::read(reader, result[i]);
    }
  }
};

// Specialization for std::vector
template<typename T>
struct TypeHandler<std::vector<T>>
{
  static constexpr TypeTag tag = TypeTag::Array;

  static void write(mpack_writer_t * writer, const std::vector<T> & vec)
  {
    mpack_start_array(writer, vec.size());
    for (const auto & item : vec) {
      TypeHandler<T>::write(writer, item);
    }
    mpack_finish_array(writer);
  }

  static void read(mpack_reader_t * reader, std::vector<T> & result)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_array) {
      throw std::runtime_error("Expected array");
    }

    result.resize(tag.v.n);
    for (uint32_t i = 0; i < tag.v.n; ++i) {
      TypeHandler<T>::read(reader, result[i]);
    }
  }
};

// Specialization for std::unordered_map
template<typename K, typename V>
struct TypeHandler<std::unordered_map<K, V>>
{
  static constexpr TypeTag tag = TypeTag::Map;

  static void write(mpack_writer_t * writer, const std::unordered_map<K, V> & m)
  {
    mpack_start_map(writer, m.size());
    for (const auto & kv : m) {
      TypeHandler<K>::write(writer, kv.first);
      TypeHandler<V>::write(writer, kv.second);
    }
    mpack_finish_map(writer);
  }

  static void read(mpack_reader_t * reader, std::unordered_map<K, V> & result)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_map) {
      throw std::runtime_error("Expected map");
    }

    for (uint32_t i = 0; i < tag.v.n; ++i) {
      K key;
      V value;
      TypeHandler<K>::read(reader, key);
      TypeHandler<V>::read(reader, value);
      result[std::move(key)] = std::move(value);     // ATTENTION allocation will happen if key does not exist
    }
  }
};
}
#endif // MPACK_SERIALIZE_TYPEHANDLERS_H
