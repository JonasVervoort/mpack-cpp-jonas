#include <iostream>
#include <string>
#include <array>
#include <tuple>
#include <type_traits>
#include <vector>
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
  Integer,
  Float,
  String,
  Array,
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
      mpack_write_int(writer, static_cast<int>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
      mpack_write_float(writer, static_cast<float>(value));
    } else if constexpr (is_string_like<T>::value) {
      if constexpr (std::is_same_v<T, std::string>) {
        // Handle std::string
        // If max_length is specified and string is longer, truncate
        if (max_length > 0 && value.length() > max_length) {
          mpack_write_str(writer, value.c_str(), max_length);
        } else {
          mpack_write_cstr(writer, value.c_str());
        }
      } else {
        // Handle const char* or char[]
        mpack_write_cstr(writer, value);
      }
    } else if constexpr (is_serializable_v<T>) {
      // Handle Serializable objects
      value.serialize(writer);
    } else {
      static_assert(
        std::is_same_v<T, void>,
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
        // Use fixed-size buffer instead of dynamic allocation
        char buffer[max_length + 1]; // +1 for null terminator
        mpack_expect_cstr(reader, buffer, sizeof(buffer));
        return std::string(buffer);
      } else {
        // Fall back to dynamic allocation for unlimited strings
        char * str = mpack_expect_cstr_alloc(reader, 256);
        std::string result(str);
        MPACK_FREE(str);
        return result;
      }
    } else if constexpr (is_string_like<T>::value) {
      // Handle const char* - this is simplified

      mpack_expect_cstr(reader, buffer, sizeof(buffer));
      return buffer;
    } else if constexpr (is_serializable_v<T>) {
      T obj;
      obj.deserialize(reader);
      return obj;
    } else {
      static_assert(
        std::is_same_v<T, void>,
        "Type is not deserializable and does not match any known type");
      return T{};
    }
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

// Field descriptor for reflecting on struct members
template<typename T, typename MemberType>
struct Field
{
  const char * name;
  MemberType T::* ptr;
  size_t max_string_length = 0;  // Default 0 means no limit for non-string types
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

// First serializable class - needs to be defined before being used in UserInfo
struct MyData : public MsgPackSerializable<MyData>
{
  std::string name;
  int version;
  std::array<int, 3> array;

  // Default constructor required for deserialization
  MyData()
  : name(""), version(0), array({0, 0, 0}) {}

  // Custom constructor for convenience
  MyData(const std::string & n, int v, const std::array<int, 3> & a)
  : name(n), version(v), array(a) {}

  // Define reflection information for the struct
  static constexpr auto get_fields()
  {
    using namespace serialization;
    return std::make_tuple(
      make_field("name", &MyData::name, 20),
      make_field("version", &MyData::version),
      make_field("array", &MyData::array)
    );
  }
};

// Second serializable class with nested MyData object
struct UserInfo : public MsgPackSerializable<UserInfo>
{
  std::string username;
  int user_id;
  std::vector<std::string> roles;
  MyData metadata;   // Nested serializable object

  // Default constructor required for deserialization
  UserInfo()
  : username(""), user_id(0), roles(), metadata() {}
  // Custom constructor
  UserInfo(
    const std::string & uname, int id,
    const std::vector<std::string> & r, const MyData & md)
  : username(uname), user_id(id), roles(r), metadata(md)
  {
  }

  static constexpr auto get_fields()
  {
    using namespace serialization;
    return std::make_tuple(
      make_field("username", &UserInfo::username),
      make_field("user_id", &UserInfo::user_id),
      make_field("roles", &UserInfo::roles),
      make_field("metadata", &UserInfo::metadata)
    );
  }


};

// Function to print MyData contents for verification
void print_data(const MyData & data)
{
  std::cout << "MyData contents:\n";
  std::cout << "  Name: " << data.name << '\n';
  std::cout << "  Version: " << data.version << '\n';
  std::cout << "  Array: [";
  for (size_t i = 0; i < data.array.size(); ++i) {
    if (i > 0) {std::cout << ", ";}
    std::cout << data.array[i];
  }
  std::cout << "]\n";
}

// Function to print UserInfo contents
void print_user(const UserInfo & user)
{
  std::cout << "UserInfo contents:\n";
  std::cout << "  Username: " << user.username << '\n';
  std::cout << "  User ID: " << user.user_id << '\n';
  std::cout << "  Roles: [";
  for (size_t i = 0; i < user.roles.size(); ++i) {
    if (i > 0) {std::cout << ", ";}
    std::cout << user.roles[i];
  }
  std::cout << "]\n";
  std::cout << "  Metadata:\n";
  std::cout << "    Name: " << user.metadata.name << '\n';
  std::cout << "    Version: " << user.metadata.version << '\n';
  std::cout << "    Array: [";
  for (size_t i = 0; i < user.metadata.array.size(); ++i) {
    if (i > 0) {std::cout << ", ";}
    std::cout << user.metadata.array[i];
  }
  std::cout << "]\n";
}

int main()
{
  // Create a MyData instance
  MyData data("TestData", 42, {10, 20, 30});

  // Create a UserInfo instance with nested MyData
  UserInfo user("johndoe", 12345,
    {"admin", "developer", "tester"},
    data);

  std::cout << "Original user with nested data:\n";
  print_user(user);

  // Serialize to MessagePack buffer
  static constexpr size_t buffer_size = 1024;
  std::array<char, buffer_size> buffer;
  // fill with zero's
  buffer.fill(0);
  const auto size_of_msg = user.to_msgpack(buffer);
  std::cout << "Serialized to " << std::to_string(size_of_msg) << " bytes\n";
  
  // Deserialize from buffer
  UserInfo restored_user;
  Serializable::from_msgpack(buffer, restored_user);

  std::cout << "\nDeserialized user with nested data:\n";
  print_user(restored_user);


  return 0;
}
