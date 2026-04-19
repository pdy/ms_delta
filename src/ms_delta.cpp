#include <optional>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <errhandlingapi.h>
#include <msdelta.h>

namespace fs = std::filesystem;
struct Args
{
  enum class Command
  {
    Create,
    Apply
  } cmd;

  std::string source, target, patches;
};

struct StdStringClearGuard
{
  std::string &str;
  StdStringClearGuard(std::string &str_)
    : str{str_}
  {}

  ~StdStringClearGuard() { str.clear(); }
};

namespace {

void usage()
{
  std::cout << 
    "\nCreates delta diffs between name matching files in two folders."
    "\nGeneral usage:\n"
    "\tns_delta_patches <command> -s <source> -t <target> -p <patches>\n\n"
    "\t<command> - create/apply\n"
    "\t-s        - source catalog path REQUIRED\n"
    "\t-t        - target catalog path REQUIRED\n"
    "\t-p        - patches catalog path REQUIRED\n\n"
    "Example create:\n"
    "\tns_delta_patches create -s \"C:\\source_data\" -t \"C:\\target_data\" -p \"C:\\patches\"\n\n"
    "Example apply:\n"
    "\tns_delta_patches apply -s \"C:\\target_data\" -t \"C:\\applied_data_patches\" -p \"C:\\patches\"\n\n"
    ;
}

std::optional<Args> process_args(int argc, char *argv[])
{
  if(argc < 2)
    return std::nullopt;

  Args ret;
  const std::string_view cmd{ argv[1] };
  if(cmd == "create")
    ret.cmd = Args::Command::Create;
  else if(cmd == "apply")
    ret.cmd = Args::Command::Apply;
  else {
    std::cout << "Invalid command provided [" << cmd <<"] available - create/apply\n";
    return std::nullopt;
  }

  for(size_t i = 2; i < static_cast<size_t>(argc); ++i) {
    const std::string_view arg{ argv[i] };
    if(arg == "-s")
      ret.source = argv[++i];
    else if(arg == "-t")
      ret.target = argv[++i];
    else if(arg == "-p")
      ret.patches = argv[++i];
  }

  bool ok = true;
  if(ret.source.empty()) {
    std::cout << "Missing -s argument\n";
    ok = false;
  }

  if(ret.target.empty()) {
    std::cout << "Missing -t argument\n";
    ok = false;
  }

  if(ret.patches.empty()) {
    std::cout << "Missing -p argument\n";
    ok = false;
  }

  if(!ok)
    return std::nullopt;

  return ret;
}

std::string Win32ErrStr(unsigned long errCode)
{
  // https://learn.microsoft.com/en-us/windows/win32/sysinfo/getting-system-information

  char sysMsg[MAX_PATH] = {'\0'};
  char* p = sysMsg;

  ::FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM |
         FORMAT_MESSAGE_IGNORE_INSERTS,
         nullptr, errCode,
         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
         sysMsg, MAX_PATH, nullptr );

  // Trim the end of the line and terminate it with a null
  // 9 - \t (horizontal tab)
  // [0 - 32) - All characters in this area excepting 9
  // 46 - . (dot)
  while (*p++)
  {
    if ((*p != 9 && *p < 32) || *p == 46)
    {
      *p = 0;
      break;
    }
  }

  return sysMsg;
}

std::vector<fs::path> list_files(std::string_view path)
{
  std::error_code ec;
  if(!fs::is_directory(path, ec))
    return {};

  std::vector<fs::path> ret;
  ret.reserve(10);
  for(const auto &entry : fs::directory_iterator(path)) {
    if(entry.is_regular_file(ec))
      ret.push_back(entry.path());
  }

  return ret;
}

int run_create(const Args &args)
{
  std::error_code ec;
  if(!fs::is_directory(args.patches, ec)) {
    std::cout << "Patches does not exist as a directory [" << args.patches << "]\n";
    return ERROR_PATH_NOT_FOUND;
  }

  const auto source_files = list_files(args.source);
  if(source_files.empty()) {
    std::cout << "Source folder [" << args.source << "] is empty or does not exists\n";
    return ERROR_FILE_NOT_FOUND;
  }

  const auto target_files = list_files(args.target);
  if(target_files.empty()) {
    std::cout << "Target folder [" << args.target << "] is empty or does not exists\n";
    return ERROR_FILE_NOT_FOUND;
  }

  const auto find_target = [&target_files](const fs::path &p) -> std::optional<fs::path> {
    auto found = std::find_if(target_files.begin(), target_files.end(), [&p](const auto &e) { return p.filename() == e.filename(); }); 
    if(found == target_files.end())
      return std::nullopt;

    return *found;
  };

  std::string patch_file_path;
  for(const fs::path &src : source_files) {
    StdStringClearGuard str_clear(patch_file_path);

    const auto target = find_target(src);
    if(!target) {
      std::cout << "\tNo mathing target for " << src.filename() << " - skipping.\n";
      continue;
    }

    std::format_to(std::back_inserter(patch_file_path), "{}\\{}.patch", args.patches, src.filename().stem().string());

    if(fs::exists(patch_file_path, ec)) {
      std::cout << "\tPatch file [" << patch_file_path << "] already exists - skipping\n";
      continue;
    }

    std::cout << "\tRunning delta for " << src.filename() << " saving to [" << patch_file_path << "]\n";

    const BOOL ok = CreateDeltaA(
      DELTA_FILE_TYPE_SET_RAW_ONLY,
      DELTA_FLAG_IGNORE_FILE_SIZE_LIMIT,
      DELTA_FLAG_NONE,
      src.string().c_str(),
      target->string().c_str(),
      nullptr,
      nullptr,
      DELTA_INPUT{0},
      nullptr,
      CALG_MD5,
      patch_file_path.c_str()
    );

    if(!ok) {
      const auto error_code = ::GetLastError();
      std::cout << "\t  Failed with error code [" <<  error_code << " - " << Win32ErrStr(error_code) << "]\n";
    }
  }

  return 0;
}

int run_apply(const Args &args)
{
  std::error_code ec;
  if(!fs::is_directory(args.target, ec)) {
    std::cout << "Target does not exist as a directory [" << args.target << "]\n";
    return ERROR_PATH_NOT_FOUND;
  }

  const auto source_files = list_files(args.source);
  if(source_files.empty()) {
    std::cout << "Source folder [" << args.source << "] is empty or does not exists\n";
    return ERROR_FILE_NOT_FOUND;
  }

  const auto patch_files = list_files(args.patches);
  if(patch_files.empty()) {
    std::cout << "Patches folder [" << args.target << "] is empty or does not exists\n";
    return ERROR_FILE_NOT_FOUND;
  }

  const auto find_patch = [&patch_files](const fs::path &file) -> std::optional<fs::path> {
    auto found = std::find_if(patch_files.begin(), patch_files.end(), [&file](const auto &patch) { return file.filename().stem() == patch.filename().stem(); });
    if(found == patch_files.end())
      return std::nullopt;

    return *found;
  };

  std::string target_file_path;
  for(const fs::path &src : source_files) {
    StdStringClearGuard str_clear(target_file_path);

    const auto patch_file = find_patch(src);
    if(!patch_file) {
      std::cout << "\tNo patch for " << src << "- skipping.\n";
      continue;
    }

    std::format_to(std::back_inserter(target_file_path), "{}\\{}", args.target, src.filename().string());
    
    if(fs::exists(target_file_path, ec)) {
      std::cout << "\tTarget file [" << target_file_path << "] alread exists - skipping\n";
      continue;
    }

    std::cout << "\tRunning apply delta for " << src.filename() << " saving to [" << target_file_path << "]\n";

    const BOOL ok = ApplyDeltaA(
      DELTA_FLAG_IGNORE_FILE_SIZE_LIMIT,
      src.string().c_str(),
      patch_file->string().c_str(),
      target_file_path.c_str()
    );

    if(!ok) {
      const auto error_code = ::GetLastError();
      std::cout << "\t  Failed with error code [" <<  error_code << " - " << Win32ErrStr(error_code) << "]\n";
    }
  }

  return 0;
}

} // namespace

int main(int argc, char *argv[])
{
  const auto args = process_args(argc, argv);
  if(!args) {
    usage();
    return ERROR_BAD_ARGUMENTS;
  }

  if(args->cmd == Args::Command::Create)
    return run_create(*args);

  if(args->cmd == Args::Command::Apply)
    return run_apply(*args);

  return 0;
}
