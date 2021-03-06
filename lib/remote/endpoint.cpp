/* Icinga 2 | (c) 2012 Icinga GmbH | GPLv2+ */

#include "remote/endpoint.hpp"
#include "remote/endpoint-ti.cpp"
#include "remote/apilistener.hpp"
#include "remote/jsonrpcconnection.hpp"
#include "remote/zone.hpp"
#include "base/configtype.hpp"
#include "base/utility.hpp"
#include "base/exception.hpp"
#include "base/convert.hpp"

using namespace icinga;

REGISTER_TYPE(Endpoint);

boost::signals2::signal<void(const Endpoint::Ptr&, const JsonRpcConnection::Ptr&)> Endpoint::OnConnected;
boost::signals2::signal<void(const Endpoint::Ptr&, const JsonRpcConnection::Ptr&)> Endpoint::OnDisconnected;

void Endpoint::OnAllConfigLoaded()
{
	ObjectImpl<Endpoint>::OnAllConfigLoaded();

	String zoneName = GetZoneName();

	if (zoneName != "") {
		Zone::Ptr zone = Zone::GetByName(zoneName);
		if (!zone) {
			BOOST_THROW_EXCEPTION(ScriptError("Endpoint '" + GetName() +
				"' cannot belong to zone '" + zoneName +
				"', zone does not exist.", GetDebugInfo()));
		}

		zone->AddEndpoint(this);

		// Adjust runtime zone attr value so that object is treated appropriately.
		Zone::Ptr parent = zone->GetParent();
		if (parent) {
			Log(LogDebug, "Endpoint")
				<< "Update endpoint '" << GetName() << "' zone attribute to '" << parent->GetName() <<
				"' (retain endpoint object sync).";
			SetZoneName(parent->GetName());
		}

		// XXX: If/When AddConnection() becomes public
		// Attempt to connect to endpoint.
		Zone::Ptr local = Zone::GetLocalZone();
		if (parent == local) {
			ApiListener::Ptr listener = ApiListener::GetInstance();
			if (listener) {
				for (const JsonRpcConnection::Ptr& aclient : listener->GetAnonymousClients()) {
					if (aclient->GetIdentity() == GetName()) {
						aclient->Disconnect();
					}
				}
				listener->AddConnection(this);
			}

		}

	}

	if (!m_Zone)
		BOOST_THROW_EXCEPTION(ScriptError("Endpoint '" + GetName() +
			"' does not belong to a zone.", GetDebugInfo()));
}

void Endpoint::Stop(bool runtimeRemoved)
{
	ObjectImpl<Endpoint>::Stop(runtimeRemoved);

	Log(LogWarning, "ApiListener")
			<< "---------------------------------- Stop. Removing API client for endpoint '" << GetName() << "'.";

	for (const JsonRpcConnection::Ptr& client : this->GetClients()) {
		client->Disconnect();
	}

	if (m_Zone)
		m_Zone->RemoveEndpoint(this);
}

void Endpoint::SetCachedZone(const Zone::Ptr& zone)
{
	if (!m_Zone)
		m_Zone = zone;
}

void Endpoint::AddClient(const JsonRpcConnection::Ptr& client)
{
	bool was_master = ApiListener::GetInstance()->IsMaster();

	{
		boost::mutex::scoped_lock lock(m_ClientsLock);
		m_Clients.insert(client);
	}

	bool is_master = ApiListener::GetInstance()->IsMaster();

	if (was_master != is_master)
		ApiListener::OnMasterChanged(is_master);

	OnConnected(this, client);
}

void Endpoint::RemoveClient(const JsonRpcConnection::Ptr& client)
{
	bool was_master = ApiListener::GetInstance()->IsMaster();

	{
		boost::mutex::scoped_lock lock(m_ClientsLock);
		m_Clients.erase(client);


		Log(LogWarning, "ApiListener")
			<< "---------------------------------- RemoveClient. Removing API client for endpoint '" << GetName() << "'. " << m_Clients.size() << " API clients left.";

		SetConnecting(false);
	}

	bool is_master = ApiListener::GetInstance()->IsMaster();

	if (was_master != is_master)
		ApiListener::OnMasterChanged(is_master);

	OnDisconnected(this, client);
}

std::set<JsonRpcConnection::Ptr> Endpoint::GetClients() const
{
	boost::mutex::scoped_lock lock(m_ClientsLock);
	return m_Clients;
}

Zone::Ptr Endpoint::GetZone() const
{
	return m_Zone;
}

bool Endpoint::GetConnected() const
{
	boost::mutex::scoped_lock lock(m_ClientsLock);
	return !m_Clients.empty();
}

Endpoint::Ptr Endpoint::GetLocalEndpoint()
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	if (!listener)
		return nullptr;

	return listener->GetLocalEndpoint();
}

void Endpoint::AddMessageSent(int bytes)
{
	double time = Utility::GetTime();
	m_MessagesSent.InsertValue(time, 1);
	m_BytesSent.InsertValue(time, bytes);
	SetLastMessageSent(time);
}

void Endpoint::AddMessageReceived(int bytes)
{
	double time = Utility::GetTime();
	m_MessagesReceived.InsertValue(time, 1);
	m_BytesReceived.InsertValue(time, bytes);
	SetLastMessageReceived(time);
}

double Endpoint::GetMessagesSentPerSecond() const
{
	return m_MessagesSent.CalculateRate(Utility::GetTime(), 60);
}

double Endpoint::GetMessagesReceivedPerSecond() const
{
	return m_MessagesReceived.CalculateRate(Utility::GetTime(), 60);
}

double Endpoint::GetBytesSentPerSecond() const
{
	return m_BytesSent.CalculateRate(Utility::GetTime(), 60);
}

double Endpoint::GetBytesReceivedPerSecond() const
{
	return m_BytesReceived.CalculateRate(Utility::GetTime(), 60);
}
