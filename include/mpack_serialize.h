#ifndef MPACK_SERIALIZE_H
#define MPACK_SERIALIZE_H 

#include <iostream>
#include <string>
#include <array>
#include <tuple>
#include <type_traits>
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

  static void write(mpack_writer_t * writer, const T & value, size_t max_length = 0)
  {
    if constexpr (std::is_integral_v<T>) {
      mpack_write_int(writer, static_cast<int64_t>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
      mpack_write_float(writer, static_cast<float>(value));
    } else if constexpr (is_string_like<T>::value) {
      if constexpr (std::is_same_v<T, std::string>) {
        if (max_length > 0 && value.length() > max_length) {
          mpack_write_str(writer, value.c_str(), max_length);
        } else {
          mpack_write_cstr(writer, value.c_str());
        }
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

  static T read(mpack_reader_t * reader, size_t max_length = 0)
  {
    if constexpr (std::is_integral_v<T>) {
      return static_cast<T>(mpack_expect_int(reader));
    } else if constexpr (std::is_floating_point_v<T>) {
      return static_cast<T>(mpack_expect_float(reader));
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (max_length > 0) {
        char buffer[max_length + 1];
        mpack_expect_cstr(reader, buffer, sizeof(buffer));
        return std::string(buffer);
      } else {
        char * str = mpack_expect_cstr_alloc(reader, 256);
        std::string result(str);
        MPACK_FREE(str);
        return result;
      }
    } else if constexpr (is_serializable_v<T>) {
      T obj;
      obj.deserialize(reader);
      return obj;
    } else {
      static_assert(
        sizeof(T) == 0,
        "Type is not deserializable and does not match any known type");
      return T{};
    }
  }
};

// Specialization for bool
template<>
struct TypeHandler<bool>
{
  static constexpr TypeTag tag = TypeTag::Bool;

  static void write(mpack_writer_t * writer, bool value, size_t = 0)
  {
    mpack_write_bool(writer, value);
  }

  static bool read(mpack_reader_t * reader, size_t = 0)
  {
    return mpack_expect_bool(reader);
  }
};

// Specialization for unsigned integers
template<typename T>
struct TypeHandler<T, std::enable_if_t<std::is_unsigned_v<T> && !std::is_same_v<T, bool>>> 
{
  static constexpr TypeTag tag = TypeTag::UInt;

  static void write(mpack_writer_t * writer, T value, size_t = 0)
  {
    mpack_write_uint(writer, static_cast<uint64_t>(value));
  }

  static T read(mpack_reader_t * reader, size_t = 0)
  {
    return static_cast<T>(mpack_expect_u32(reader));
  }
};

// Specialization for double
template<>
struct TypeHandler<double>
{
  static constexpr TypeTag tag = TypeTag::Double;

  static void write(mpack_writer_t * writer, double value, size_t = 0)
  {
    mpack_write_double(writer, value);
  }

  static double read(mpack_reader_t * reader, size_t = 0)
  {
    return mpack_expect_double(reader);
  }
};

// Specialization for std::optional
template<typename U>
struct TypeHandler<std::optional<U>>
{
  static constexpr TypeTag tag = TypeTag::Nil;

  static void write(mpack_writer_t * writer, const std::optional<U> & opt, size_t max_length = 0)
  {
    if (opt.has_value()) {
      TypeHandler<U>::write(writer, *opt, max_length);
    } else {
      mpack_write_nil(writer);
    }
  }

  static std::optional<U> read(mpack_reader_t * reader, size_t max_length = 0)
  {
    mpack_tag_t tag = mpack_peek_tag(reader);
    if (tag.type == mpack_type_nil) {
      mpack_expect_nil(reader);
      return std::nullopt;
    }
    return TypeHandler<U>::read(reader, max_length);
  }
};

// // Specialization for binary data (vector<char>)
// template<>
// struct TypeHandler<std::vector<char>>
// {
//   static constexpr TypeTag tag = TypeTag::Binary;

//   static void write(mpack_writer_t * writer, const std::vector<char> & data, size_t = 0)
//   {
//     mpack_write_bin(writer, data.data(), data.size());
//   }

//   static std::vector<char> read(mpack_reader_t * reader, size_t = 0)
//   {
//     mpack_tag_t tag = mpack_read_tag(reader);
//     if (tag.type != mpack_type_bin) {
//       throw std::runtime_error("Expected binary data");
//     }
//     std::vector<char> result(tag.v.n);
//     mpack_expect_bin(reader, result.data(), result.size());
//     return result;
//   }
// };

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

  static std::array<T, N> read(mpack_reader_t * reader)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_array || tag.v.n != N) {
      throw std::runtime_error("Expected array of specific size");
    }

    std::array<T, N> result;
    for (size_t i = 0; i < N; ++i) {
      result[i] = TypeHandler<T>::read(reader);
    }
    return result;
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

  static std::vector<T> read(mpack_reader_t * reader)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_array) {
      throw std::runtime_error("Expected array");
    }

    std::vector<T> result;
    result.reserve(tag.v.n);
    for (uint32_t i = 0; i < tag.v.n; ++i) {
      result.push_back(TypeHandler<T>::read(reader));
    }
    return result;
  }
};

// Specialization for std::map
template<typename K, typename V>
struct TypeHandler<std::map<K, V>>
{
  static constexpr TypeTag tag = TypeTag::Map;

  static void write(mpack_writer_t * writer, const std::map<K, V> & m)
  {
    mpack_start_map(writer, m.size());
    for (const auto & kv : m) {
      TypeHandler<K>::write(writer, kv.first);
      TypeHandler<V>::write(writer, kv.second);
    }
    mpack_finish_map(writer);
  }

  static std::map<K, V> read(mpack_reader_t * reader)
  {
    mpack_tag_t tag = mpack_read_tag(reader);
    if (tag.type != mpack_type_map) {
      throw std::runtime_error("Expected map");
    }
    std::map<K, V> result;
    for (uint32_t i = 0; i < tag.v.n; ++i) {
      K key = TypeHandler<K>::read(reader);
      V value = TypeHandler<V>::read(reader);
      result.emplace(key, value);
    }
    return result;
  }
};

// Field descriptor for reflecting on struct members
template<typename T, typename MemberType>
struct Field
{
  const char * name;
  MemberType T::* ptr;
  size_t max_string_length = 0;
};

// Helper to create field descriptors
template<typename T, typename MemberType>
constexpr auto make_field(const char * name, MemberType T::* ptr, size_t max_string_length = 0)
{
  return Field<T, MemberType>{name, ptr, max_string_length};
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
  size_t to_msgpack(std::array<char,N> & buffer) const
  {
    mpack_writer_t writer;
    mpack_writer_init(&writer, buffer.data(), buffer.size());

    serialize(&writer);

    size_t actual_size = mpack_writer_buffer_used(&writer);
    if (mpack_writer_destroy(&writer) == mpack_ok) {
      return actual_size;
    } else {
      return 0;
    }
  }

  // Helper for deserialization from buffer
  template<size_t N>
  static void from_msgpack(const std::array<char,N> & buffer, Serializable & obj)
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
      static_cast<const Derived *>(this)->*(field.ptr));
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
      derived->*(field.ptr) = serialization::TypeHandler<MemberType>::read(reader);
      handled = true;
    }
  }
};
#endif // MPACK_SERIALIZE_H