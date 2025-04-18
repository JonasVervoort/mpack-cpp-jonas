#ifndef MPACK_SERIALIZE_H
#define MPACK_SERIALIZE_H

#include <iostream>
#include <string>
#include <array>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <optional>
#include <map>
#include <cstdint>
#include "mpack/mpack.h"

// Forward declarations
class Serializable;
template<typename Derived>
class MsgPackSerializable;

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
    
    result.clear();
    for (uint32_t i = 0; i < tag.v.n; ++i) {
      K key;
      V value;
      TypeHandler<K>::read(reader, key);
      TypeHandler<V>::read(reader, value);
      result.emplace(std::move(key), std::move(value));
    }
  }
};

// Field descriptor for reflecting on struct members
template<typename T, typename MemberType>
struct Field
{
  const char * name;
  MemberType T::* member_ptr;
};

// Helper to create field descriptors
template<typename T, typename MemberType>
constexpr auto make_field(const char * name, MemberType T::* member_ptr)
{
  return Field<T, MemberType>{name, member_ptr};
}

} // namespace serialization
/**
 * Base class for serializable objects
 */
class Serializable
{
public:
  virtual ~Serializable() = default;

  // Public serialization interface
  void serialize(mpack_writer_t * writer) const
  {
    // Call implementation
    do_serialize(writer);
  }

  void deserialize(mpack_reader_t * reader)
  {
    // Call implementation
    do_deserialize(reader);
  }

  // Helper for serialization to buffer
  template<size_t N>
  static size_t to_msgpack(std::array<char, N> & buffer, const Serializable & obj)
  {
    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer.data(), buffer.size());

    obj.serialize(&writer);

    size_t actual_size = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) == mpack_ok) {
      return actual_size;
    } else {
      return 0;
    }
  }

  // Helper for deserialization from buffer
  template<size_t N>
  static void from_msgpack(const std::array<char, N> & buffer, Serializable & obj)
  {
    mpack_reader_t reader;
    mpack_reader_init_data(&reader, buffer.data(), buffer.size());

    obj.deserialize(&reader);

    if (mpack_reader_destroy(&reader) != mpack_ok) {
      throw std::runtime_error("An error occurred decoding the data");
    }
  }

protected:
  // These methods should be overridden by derived classes
  virtual void do_serialize(mpack_writer_t * writer) const = 0;
  virtual void do_deserialize(mpack_reader_t * reader) = 0;
};

/**
 * Serializable template base for easy implementation inheritance
 */
template<typename Derived>
class MsgPackSerializable : public Serializable
{
protected:
  void do_serialize(mpack_writer_t * writer) const override
  {
    constexpr auto fields = Derived::get_fields();
    constexpr size_t field_count = std::tuple_size_v<decltype(fields)>;

    mpack_start_map(writer, field_count);
    // Compile-time iteration using index_sequence
    serialize_fields(writer, fields, std::make_index_sequence<field_count>{});

    mpack_finish_map(writer);
  }

  void do_deserialize(mpack_reader_t * reader) override
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_map) {
      throw std::runtime_error("Expected a map");
    }

    // Read all available fields
    for (uint32_t i = 0; i < tag.v.n; ++i) {
      std::string key = [reader]() {
          char * str = mpack_expect_cstr_alloc(reader, 256);
          std::string result(str);
          MPACK_FREE(str);
          return result;
        }();

      deserialize_field(reader, key);
    }
  }

private:
  // Helper for serialize: unpack tuple at compile time
  template<typename Tuple, size_t... I>
  void serialize_fields(
    mpack_writer_t * writer, const Tuple & tuple,
    std::index_sequence<I...>) const
  {
    // Fold expression to handle all fields
    (serialize_field(writer, std::get<I>(tuple)), ...);
  }

  // Serialize a single field
  template<typename T, typename MemberType>
  void serialize_field(
    mpack_writer_t * writer,
    const serialization::Field<T, MemberType> & field) const
  {
    mpack_write_cstr(writer, field.name);
    serialization::TypeHandler<MemberType>::write(
      writer,
      static_cast<const Derived *>(this)->*(field.member_ptr));
  }

  // Deserialize a specific field by name
  void deserialize_field(mpack_reader_t * reader, const std::string & key)
  {
    constexpr auto fields = Derived::get_fields();
    deserialize_field_impl(
      reader, key, fields,
      std::make_index_sequence<std::tuple_size_v<decltype(fields)>>{});
  }

  // Implementation helper for deserialize_field
  template<typename Tuple, size_t... I>
  void deserialize_field_impl(
    mpack_reader_t * reader, const std::string & key,
    const Tuple & tuple, std::index_sequence<I...>)
  {
    bool field_handled = false;

    // Using fold expression to try each field
    ((try_deserialize_field(reader, key, std::get<I>(tuple), field_handled)), ...);

    // If no field matched, skip the value
    if (!field_handled) {
      mpack_discard(reader);
    }
  }

  // Try to deserialize a particular field if the key matches
  template<typename T, typename MemberType>
  void try_deserialize_field(
    mpack_reader_t * reader, const std::string & key,
    const serialization::Field<T, MemberType> & field, bool & handled)
  {
    if (!handled && key == field.name) {
      Derived * derived = static_cast<Derived *>(this);
      serialization::TypeHandler<MemberType>::read(reader, derived->*(field.member_ptr));
      handled = true;
    }
  }
};
#endif // MPACK_SERIALIZE_H