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
#include "include/mpack_serialize.h"


// First serializable class - needs to be defined before being used in UserInfo
struct MyData : public MsgPackSerializable<MyData>
{
  std::string name;
  int version;
  std::array<int, 3> array;
  std::array<char, 20> cstr; // Example of a fixed-size array

  double my_double=3.3; // Example of additional field
  std::optional<int> optional_value; // Example of optional field
  std::unordered_map<int, double> my_map; // Example of map field

  // Default constructor required for deserialization
  MyData()
  : name(""), version(0), array({0, 0, 0}) {
    const char* content = "default";
    std::copy(content, content + 20, cstr.begin());
  }

  // Custom constructor for convenience
  MyData(const std::string & n, int v, const std::array<int, 3> & a)
  : name(n), version(v), array(a) {}

  // Define reflection information for the struct
  static constexpr auto get_fields()
  {
    using namespace serialization;
    return std::make_tuple(
      make_field("name", &MyData::name),
      make_field("version", &MyData::version),
      make_field("array", &MyData::array),
      make_field("haha", &MyData::my_double),
      make_field("optional_value", &MyData::optional_value),
      make_field("my_map", &MyData::my_map),
      make_field("cstr", &MyData::cstr)
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
  std::cout << "    Version::: " << user.metadata.version << '\n';
  std::cout << "    myDouble: " << user.metadata.my_double << '\n';
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
  const auto size_of_msg = Serializable::to_msgpack(buffer, user);
  std::cout << "Serialized to " << std::to_string(size_of_msg) << " bytes\n";
  
  // Deserialize from buffer
  UserInfo restored_user;
  Serializable::from_msgpack(buffer, restored_user);

  std::cout << "\nDeserialized user with nested data:\n";
  print_user(restored_user);


  return 0;
}
