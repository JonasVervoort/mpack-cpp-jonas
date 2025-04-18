#ifndef MPACK_SERIALIZER_H
#define MPACK_SERIALIZER_H

#include <cstdint>
#include <tuple>
#include <type_traits>


#include "mpack/mpack.h"

// Serialization framework
namespace serialization
{

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

}  // namespace serialization

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

    // Calculate max field name length at compile time
    constexpr auto fields = Derived::get_fields();
    constexpr size_t max_field_name_length = calc_max_field_name_length(fields);

    // Use stack-allocated buffer for field names
    char key_buffer[max_field_name_length + 1]; // +1 for null terminator

    // Read all available fields
    for (uint32_t i = 0; i < tag.v.n; ++i) {
      // Read directly into our stack buffer with a size limit
      mpack_tag_t key_tag = mpack_peek_tag(reader);
      if (key_tag.type != mpack_type_str) {
        throw std::runtime_error("Expected string key in map");
      }

      size_t key_length = key_tag.v.l;
      if (key_length > max_field_name_length) {
        // Skip this key-value pair if the key is too long
        mpack_discard(reader); // Skip the key
        mpack_discard(reader); // Skip the value
        continue;
      }

      // Consume the tag we peeked
      mpack_read_tag(reader);

      // Read the key into our buffer
      mpack_read_bytes(reader, key_buffer, key_length);
      key_buffer[key_length] = '\0'; // Null-terminate

      // Process the field
      deserialize_field(reader, key_buffer);
    }
  }

private:
  // C++17 compatible constexpr function to calculate max field name length
  template<typename Tuple>
  static constexpr size_t calc_max_field_name_length(const Tuple & tuple)
  {
    return calc_max_field_name_length_impl(
      tuple,
      std::make_index_sequence<std::tuple_size_v<Tuple>>{});
  }

  template<typename Tuple, size_t... I>
  static constexpr size_t calc_max_field_name_length_impl(
    const Tuple & tuple,
    std::index_sequence<I...>)
  {
    size_t max_len = 0;
    ((max_len = std::max(max_len, strlen(std::get<I>(tuple).name))), ...);
    return max_len;
  }

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
  void deserialize_field(mpack_reader_t * reader, const char * key)
  {
    constexpr auto fields = Derived::get_fields();
    deserialize_field_impl(
      reader, key, fields,
      std::make_index_sequence<std::tuple_size_v<decltype(fields)>>{});
  }

  // Implementation helper for deserialize_field
  template<typename Tuple, size_t... I>
  void deserialize_field_impl(
    mpack_reader_t * reader, const char * key,
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
    mpack_reader_t * reader, const char * key,
    const serialization::Field<T, MemberType> & field, bool & handled)
  {
    if (!handled && strcmp(key, field.name) == 0) {
      Derived * derived = static_cast<Derived *>(this);
      serialization::TypeHandler<MemberType>::read(
        reader,
        derived->*(field.member_ptr)
      );
      handled = true;
    }
  }
};
#endif  // MPACK_SERIALIZE_H
