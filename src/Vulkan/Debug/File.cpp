
#include "File.hpp"

#include <mutex>
#include <unordered_set>

namespace
{

////////////////////////////////////////////////////////////////////////////////
// FileBase
////////////////////////////////////////////////////////////////////////////////
class FileBase : public vk::dbg::File
{
public:
	FileBase(ID id, const std::string& dir, const std::string& name);

	void clearBreakpoints() override;
	void addBreakpoint(int line) override;
	bool hasBreakpoint(int line) const override;

private:
	mutable std::mutex breakpointMutex;
	std::unordered_set<int> breakpoints;  // guarded by breakpointMutex
};

FileBase::FileBase(ID id, const std::string& dir, const std::string& name) :
    File(id, dir, name) {}

void FileBase::clearBreakpoints()
{
	std::unique_lock<std::mutex> lock(breakpointMutex);
	breakpoints.clear();
}

void FileBase::addBreakpoint(int line)
{
	std::unique_lock<std::mutex> lock(breakpointMutex);
	breakpoints.emplace(line);
}

bool FileBase::hasBreakpoint(int line) const
{
	std::unique_lock<std::mutex> lock(breakpointMutex);
	return breakpoints.count(line) > 0;
}

////////////////////////////////////////////////////////////////////////////////
// VirtualFile
////////////////////////////////////////////////////////////////////////////////
class VirtualFile : public FileBase
{
public:
	VirtualFile(ID id, const std::string& name, const std::string& source);

	bool isVirtual() const override;
	std::string source() const override;

private:
	const std::string src;
};

VirtualFile::VirtualFile(ID id, const std::string& name, const std::string& source) :
    FileBase(id, "", name),
    src(source) {}

bool VirtualFile::isVirtual() const
{
	return true;
}

std::string VirtualFile::source() const
{
	return src;
}

////////////////////////////////////////////////////////////////////////////////
// PhysicalFile
////////////////////////////////////////////////////////////////////////////////
struct PhysicalFile : public FileBase
{
	PhysicalFile(ID id,
	             const std::string& dir,
	             const std::string& name);

	bool isVirtual() const override;
	std::string source() const override;
};

PhysicalFile::PhysicalFile(ID id,
                           const std::string& dir,
                           const std::string& name) :
    FileBase(id, dir, name) {}

bool PhysicalFile::isVirtual() const
{
	return false;
}

std::string PhysicalFile::source() const
{
	return "";
}

}  // anonymous namespace

namespace vk
{
namespace dbg
{

std::shared_ptr<File> File::createVirtual(ID id, const std::string& name, const std::string& source)
{
	return std::make_shared<VirtualFile>(id, name, source);
}

std::shared_ptr<File> File::createPhysical(ID id, const std::string& path)
{
	auto pathstr = std::string(path);
	auto pos = pathstr.rfind("/");
	if(pos != std::string::npos)
	{
		auto dir = pathstr.substr(0, pos);
		auto name = pathstr.substr(pos + 1);
		return std::make_shared<PhysicalFile>(id, dir.c_str(), name.c_str());
	}
	return std::make_shared<PhysicalFile>(id, "", path);
}

}  // namespace dbg
}  // namespace vk