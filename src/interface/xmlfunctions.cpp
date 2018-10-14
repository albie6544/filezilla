#include <filezilla.h>
#include "buildinfo.h"
#include "xmlfunctions.h"
#include "xmlutils.h"
#include "Options.h"
#include <wx/ffile.h>
#include <wx/log.h>

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>

CXmlFile::CXmlFile(std::wstring const& fileName, std::string const& root)
{
	if (!root.empty()) {
		m_rootName = root;
	}
	SetFileName(fileName);
}

void CXmlFile::SetFileName(std::wstring const& name)
{
	assert(!name.empty());
	m_fileName = name;
	m_modificationTime = fz::datetime();
}

pugi::xml_node CXmlFile::Load(bool overwriteInvalid)
{
	Close();
	m_error.clear();

	wxCHECK(!m_fileName.empty(), m_element);

	std::wstring redirectedName = GetRedirectedName();

	GetXmlFile(redirectedName);
	if (!m_element) {
		std::wstring err = fz::sprintf(fztranslate("The file '%s' could not be loaded."), m_fileName);
		if (m_error.empty()) {
			err += L"\n" + fztranslate("Make sure the file can be accessed and is a well-formed XML document.");
		}
		else {
			err += L"\n" + m_error;
		}

		// Try the backup file
		GetXmlFile(redirectedName + _T("~"));
		if (!m_element) {
			// Loading backup failed.

			// Create new one if we are allowed to create empty file
			bool createEmpty = overwriteInvalid;

			// Also, if both original and backup file are empty, create new file.
			if (fz::local_filesys::get_size(fz::to_native(redirectedName)) <= 0 && fz::local_filesys::get_size(fz::to_native(redirectedName + _T("~"))) <= 0) {
				createEmpty = true;
			}

			if (createEmpty) {
				m_error.clear();
				CreateEmpty();
				m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(redirectedName));
				return m_element;
			}

			// File corrupt and no functional backup, give up.
			m_error = err;
			m_modificationTime.clear();
			return m_element;
		}


		// Loading the backup file succeeded, restore file
		bool res;
		{
			wxLogNull null;
			res = wxCopyFile(redirectedName + L"~", redirectedName);
		}
		if (!res) {
			// Could not restore backup, give up.
			Close();
			m_error = err;
			m_error += L"\n" + fz::sprintf(fztranslate("The valid backup file %s could not be restored"), redirectedName + L"~");
			m_modificationTime.clear();
			return m_element;
		}

		// We no longer need the backup
		fz::remove_file(fz::to_native(redirectedName + L"~"));
		m_error.clear();
	}

	m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(redirectedName));
	return m_element;
}

bool CXmlFile::Modified()
{
	wxCHECK(!m_fileName.empty(), false);

	if (m_modificationTime.empty())
		return true;

	fz::datetime const modificationTime = fz::local_filesys::get_modification_time(fz::to_native(m_fileName));
	if (!modificationTime.empty() && modificationTime == m_modificationTime)
		return false;

	return true;
}

void CXmlFile::Close()
{
	m_element = pugi::xml_node();
	m_document.reset();
}

void CXmlFile::UpdateMetadata()
{
	if (!m_element || std::string(m_element.name()) != "FileZilla3") {
		return;
	}

	SetTextAttribute(m_element, "version", CBuildInfo::GetVersion());

	std::string const platform =
#ifdef FZ_WINDOWS
		"windows";
#elif defined(FZ_MAC)
		"mac";
#else
		"*nix";
#endif
	SetTextAttributeUtf8(m_element, "platform", platform);
}

bool CXmlFile::Save(bool printError)
{
	m_error.clear();

	wxCHECK(!m_fileName.empty(), false);
	wxCHECK(m_document, false);

	UpdateMetadata();

	bool res = SaveXmlFile();
	m_modificationTime = fz::local_filesys::get_modification_time(fz::to_native(m_fileName));

	if (!res && printError) {
		assert(!m_error.empty());

		wxString msg = wxString::Format(_("Could not write \"%s\":"), m_fileName);
		wxMessageBoxEx(msg + _T("\n") + m_error, _("Error writing xml file"), wxICON_ERROR);
	}
	return res;
}

pugi::xml_node CXmlFile::CreateEmpty()
{
	Close();

	pugi::xml_node decl = m_document.append_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";
	decl.append_attribute("encoding") = "UTF-8";

	m_element = m_document.append_child(m_rootName.c_str());
	return m_element;
}

// Opens the specified XML file if it exists or creates a new one otherwise.
// Returns false on error.
bool CXmlFile::GetXmlFile(std::wstring const& file)
{
	Close();

	if (fz::local_filesys::get_size(fz::to_native(file)) <= 0) {
		return false;
	}

	// File exists, open it
	auto result = m_document.load_file(static_cast<wchar_t const*>(file.c_str()));
	if (!result) {
		m_error += fz::sprintf(_T("%s at offset %d."), result.description(), result.offset);
		return false;
	}

	m_element = m_document.child(m_rootName.c_str());
	if (!m_element) {
		if (m_document.first_child()) { // Beware: parse_declaration and parse_doctype can break this
			// Not created by FileZilla3
			Close();
			m_error = _("Unknown root element, the file does not appear to be generated by FileZilla.").ToStdWstring();
			return false;
		}
		m_element = m_document.append_child(m_rootName.c_str());
	}

	return true;
}

std::wstring CXmlFile::GetRedirectedName() const
{
	std::wstring redirectedName = m_fileName;
	bool isLink = false;
	if (fz::local_filesys::get_file_info(fz::to_native(redirectedName), isLink, 0, 0, 0) == fz::local_filesys::file) {
		if (isLink) {
			CLocalPath target(fz::to_wstring(fz::local_filesys::get_link_target(fz::to_native(redirectedName))));
			if (!target.empty()) {
				redirectedName = target.GetPath();
				redirectedName.pop_back();
			}
		}
	}
	return redirectedName;
}

bool CXmlFile::SaveXmlFile()
{
	bool exists = false;

	bool isLink = false;
	int flags = 0;

	wxString redirectedName = GetRedirectedName();
	if (fz::local_filesys::get_file_info(fz::to_native(redirectedName), isLink, 0, 0, &flags) == fz::local_filesys::file) {
#ifdef __WXMSW__
		if (flags & FILE_ATTRIBUTE_HIDDEN) {
			SetFileAttributes(redirectedName.c_str(), flags & ~FILE_ATTRIBUTE_HIDDEN);
		}
#endif

		exists = true;
		bool res;
		{
			wxLogNull null;
			res = wxCopyFile(redirectedName, redirectedName + _T("~"));
		}
		if (!res) {
			m_error = _("Failed to create backup copy of xml file").ToStdWstring();
			return false;
		}
	}

	struct flushing_xml_writer final : public pugi::xml_writer
	{
	public:
		static bool save(pugi::xml_document const& document, std::wstring const& filename)
		{
			flushing_xml_writer writer(filename);
			if (!writer.file_.opened()) {
				return false;
			}
			document.save(writer);

			return writer.file_.opened() && writer.file_.fsync();
		}

	private:
		flushing_xml_writer(std::wstring const& filename)
			: file_(fz::to_native(filename), fz::file::writing, fz::file::empty)
		{
		}

		virtual void write(const void* data, size_t size) override {
			if (file_.opened()) {
				if (file_.write(data, static_cast<int64_t>(size)) != static_cast<int64_t>(size)) {
					file_.close();
				}
			}
		}

		fz::file file_;
	};

	bool success = flushing_xml_writer::save(m_document, redirectedName.ToStdWstring());
	if (!success) {
		fz::remove_file(fz::to_native(redirectedName));
		if (exists) {
			wxLogNull null;
			wxRenameFile(redirectedName + _T("~"), redirectedName);
		}
		m_error = _("Failed to write xml file").ToStdWstring();
		return false;
	}

	if (exists) {
		fz::remove_file(fz::to_native(redirectedName + L"~"));
	}

	return true;
}

bool GetServer(pugi::xml_node node, ServerWithCredentials & server)
{
	wxASSERT(node);

	std::wstring host = GetTextElement(node, "Host");
	if (host.empty()) {
		return false;
	}

	int port = GetTextElementInt(node, "Port");
	if (port < 1 || port > 65535) {
		return false;
	}

	if (!server.server.SetHost(host, port)) {
		return false;
	}

	int const protocol = GetTextElementInt(node, "Protocol");
	if (protocol < 0 || protocol > ServerProtocol::MAX_VALUE) {
		return false;
	}
	server.server.SetProtocol(static_cast<ServerProtocol>(protocol));

	int type = GetTextElementInt(node, "Type");
	if (type < 0 || type >= SERVERTYPE_MAX) {
		return false;
	}

	server.server.SetType(static_cast<ServerType>(type));

	int logonType = GetTextElementInt(node, "Logontype");
	if (logonType < 0 || logonType >= static_cast<int>(LogonType::count)) {
		return false;
	}

	server.SetLogonType(static_cast<LogonType>(logonType));
	
	if (server.credentials.logonType_ != LogonType::anonymous) {
		std::wstring user = GetTextElement(node, "User");
		if (user.empty() && server.credentials.logonType_ != LogonType::interactive && server.credentials.logonType_ != LogonType::ask) {
			return false;
		}

		std::wstring pass, key;
		if (server.credentials.logonType_ == LogonType::normal || server.credentials.logonType_ == LogonType::account) {
			auto passElement = node.child("Pass");
			if (passElement) {
				std::wstring encoding = GetTextAttribute(passElement, "encoding");

				if (encoding == _T("base64")) {
					std::string decoded = fz::base64_decode(passElement.child_value());
					pass = fz::to_wstring_from_utf8(decoded);
				}
				else if (encoding == _T("crypt")) {
					pass = fz::to_wstring_from_utf8(passElement.child_value());
					server.credentials.encrypted_ = fz::public_key::from_base64(passElement.attribute("pubkey").value());
					if (!server.credentials.encrypted_) {
						pass.clear();
						server.SetLogonType(LogonType::ask);
					}
				}
				else if (!encoding.empty()) {
					server.SetLogonType(LogonType::ask);
				}
				else {
					pass = GetTextElement(passElement);
				}
			}
		}
		else if (server.credentials.logonType_ == LogonType::key) {
			key = GetTextElement(node, "Keyfile");

			// password should be empty if we're using a key file
			pass.clear();

			server.credentials.keyFile_ = key;
		}

		server.SetUser(user);
		server.credentials.SetPass(pass);

		server.credentials.account_ = GetTextElement(node, "Account");
	}

	int timezoneOffset = GetTextElementInt(node, "TimezoneOffset");
	if (!server.server.SetTimezoneOffset(timezoneOffset)) {
		return false;
	}

	wxString pasvMode = GetTextElement(node, "PasvMode");
	if (pasvMode == _T("MODE_PASSIVE")) {
		server.server.SetPasvMode(MODE_PASSIVE);
	}
	else if (pasvMode == _T("MODE_ACTIVE")) {
		server.server.SetPasvMode(MODE_ACTIVE);
	}
	else {
		server.server.SetPasvMode(MODE_DEFAULT);
	}

	int maximumMultipleConnections = GetTextElementInt(node, "MaximumMultipleConnections");
	server.server.MaximumMultipleConnections(maximumMultipleConnections);

	wxString encodingType = GetTextElement(node, "EncodingType");
	if (encodingType == _T("Auto")) {
		server.server.SetEncodingType(ENCODING_AUTO);
	}
	else if (encodingType == _T("UTF-8")) {
		server.server.SetEncodingType(ENCODING_UTF8);
	}
	else if (encodingType == _T("Custom")) {
		std::wstring customEncoding = GetTextElement(node, "CustomEncoding");
		if (customEncoding.empty()) {
			return false;
		}
		if (!server.server.SetEncodingType(ENCODING_CUSTOM, customEncoding)) {
			return false;
		}
	}
	else {
		server.server.SetEncodingType(ENCODING_AUTO);
	}

	if (CServer::ProtocolHasFeature(server.server.GetProtocol(), ProtocolFeature::PostLoginCommands)) {
		std::vector<std::wstring> postLoginCommands;
		auto element = node.child("PostLoginCommands");
		if (element) {
			for (auto commandElement = element.child("Command"); commandElement; commandElement = commandElement.next_sibling("Command")) {
				std::wstring command = fz::to_wstring_from_utf8(commandElement.child_value());
				if (!command.empty()) {
					postLoginCommands.emplace_back(std::move(command));
				}
			}
		}
		if (!server.server.SetPostLoginCommands(postLoginCommands)) {
			return false;
		}
	}

	server.server.SetBypassProxy(GetTextElementInt(node, "BypassProxy", false) == 1);
	server.server.SetName(GetTextElement_Trimmed(node, "Name"));

	if (server.server.GetName().empty()) {
		server.server.SetName(GetTextElement_Trimmed(node));
	}

	for (auto parameter = node.child("Parameter"); parameter; parameter = parameter.next_sibling("Parameter")) {
		server.server.SetExtraParameter(parameter.attribute("Name").value(), GetTextElement(parameter));
	}

	return true;
}

void SetServer(pugi::xml_node node, ServerWithCredentials const& server)
{
	if (!node) {
		return;
	}

	for (auto child = node.first_child(); child; child = node.first_child()) {
		node.remove_child(child);
	}

	ServerProtocol const protocol = server.server.GetProtocol();

	AddTextElement(node, "Host", server.server.GetHost());
	AddTextElement(node, "Port", server.server.GetPort());
	AddTextElement(node, "Protocol", protocol);
	AddTextElement(node, "Type", server.server.GetType());

	ProtectedCredentials credentials = server.credentials;

	if (credentials.logonType_ != LogonType::anonymous) {
		AddTextElement(node, "User", server.server.GetUser());

		credentials.Protect();

		if (credentials.logonType_ == LogonType::normal || credentials.logonType_ == LogonType::account) {
			std::string pass = fz::to_utf8(credentials.GetPass());

			if (credentials.encrypted_) {
				pugi::xml_node passElement = AddTextElementUtf8(node, "Pass", pass);
				if (passElement) {
					SetTextAttribute(passElement, "encoding", _T("crypt"));
					SetTextAttributeUtf8(passElement, "pubkey", credentials.encrypted_.to_base64());
				}
			}
			else {
				pugi::xml_node passElement = AddTextElementUtf8(node, "Pass", fz::base64_encode(pass));
				if (passElement) {
					SetTextAttribute(passElement, "encoding", _T("base64"));
				}
			}

			if (credentials.logonType_ == LogonType::account) {
				AddTextElement(node, "Account", credentials.account_);
			}
		}
		else if (!credentials.keyFile_.empty()) {
			AddTextElement(node, "Keyfile", credentials.keyFile_);
		}
	}
	AddTextElement(node, "Logontype", static_cast<int>(credentials.logonType_));

	AddTextElement(node, "TimezoneOffset", server.server.GetTimezoneOffset());
	switch (server.server.GetPasvMode())
	{
	case MODE_PASSIVE:
		AddTextElementUtf8(node, "PasvMode", "MODE_PASSIVE");
		break;
	case MODE_ACTIVE:
		AddTextElementUtf8(node, "PasvMode", "MODE_ACTIVE");
		break;
	default:
		AddTextElementUtf8(node, "PasvMode", "MODE_DEFAULT");
		break;
	}
	AddTextElement(node, "MaximumMultipleConnections", server.server.MaximumMultipleConnections());

	switch (server.server.GetEncodingType())
	{
	case ENCODING_AUTO:
		AddTextElementUtf8(node, "EncodingType", "Auto");
		break;
	case ENCODING_UTF8:
		AddTextElementUtf8(node, "EncodingType", "UTF-8");
		break;
	case ENCODING_CUSTOM:
		AddTextElementUtf8(node, "EncodingType", "Custom");
		AddTextElement(node, "CustomEncoding", server.server.GetCustomEncoding());
		break;
	}

	if (CServer::ProtocolHasFeature(server.server.GetProtocol(), ProtocolFeature::PostLoginCommands)) {
		std::vector<std::wstring> const& postLoginCommands = server.server.GetPostLoginCommands();
		if (!postLoginCommands.empty()) {
			auto element = node.append_child("PostLoginCommands");
			for (auto const& command : postLoginCommands) {
				AddTextElement(element, "Command", command);
			}
		}
	}

	AddTextElementUtf8(node, "BypassProxy", server.server.GetBypassProxy() ? "1" : "0");
	std::wstring const& name = server.server.GetName();
	if (!name.empty()) {
		AddTextElement(node, "Name", name);
	}

	for (auto const& parameter : server.server.GetExtraParameters()) {
		auto element = AddTextElement(node, "Parameter", parameter.second);
		SetTextAttribute(element, "Name", parameter.first);
	}
}

namespace {
struct xml_memory_writer : pugi::xml_writer
{
	size_t written{};
	char* buffer{};
	size_t remaining{};

	virtual void write(const void* data, size_t size)
	{
		if (buffer && size <= remaining) {
			memcpy(buffer, data, size);
			buffer += size;
			remaining -= size;
		}
		written += size;
	}
};
}

size_t CXmlFile::GetRawDataLength()
{
	if (!m_document) {
		return 0;
	}

	xml_memory_writer writer;
	m_document.save(writer);
	return writer.written;
}

void CXmlFile::GetRawDataHere(char* p, size_t size) // p has to big enough to hold at least GetRawDataLength() bytes
{
	if (size) {
		memset(p, 0, size);
	}
	xml_memory_writer writer;
	writer.buffer = p;
	writer.remaining = size;
	m_document.save(writer);
}

bool CXmlFile::ParseData(char* data)
{
	Close();
	m_document.load_string(data);
	m_element = m_document.child(m_rootName.c_str());
	if (!m_element) {
		Close();
	}
	return !!m_element;
}

bool CXmlFile::IsFromFutureVersion() const
{
	if (!m_element) {
		return false;
	}
	std::wstring const version = GetTextAttribute(m_element, "version");
	return CBuildInfo::ConvertToVersionNumber(CBuildInfo::GetVersion().c_str()) < CBuildInfo::ConvertToVersionNumber(version.c_str());
}
