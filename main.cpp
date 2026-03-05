#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <chrono>
#include <string>
#include <vector>
#include <sqlite3.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

// OpenDDS Includes
#include <dds/DdsDcpsInfrastructureC.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/WaitSet.h>
#include "ReplicationTypeSupportImpl.h"

using json = nlohmann::json;

// Variables Globales de Estado
std::string my_node_id;
bool is_primary = false;
uint64_t start_time_ms;
std::mutex db_mutex;
sqlite3* db;

// Estado de los pares para la elección del Primary
struct PeerState {
    uint64_t uptime_ms;
    std::chrono::steady_clock::time_point last_seen;
};
std::map<std::string, PeerState> peers;
std::mutex peers_mutex;

// Referencias a DataWriters de DDS
Replication::DBChangeDataWriter_var dbchange_dw;
Replication::TableSnapshotDataWriter_var snapshot_dw;

// --- FUNCIONES UTILITARIAS ---
uint64_t get_current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t get_uptime_ms() {
    return get_current_time_ms() - start_time_ms;
}

// --- BASE DE DATOS (SQLite) ---
void init_db() {
    // Usamos el ID del nodo para crear un archivo único, ej: "Consola_Principal.db"
    std::string db_filename = my_node_id + ".db";
    sqlite3_open(db_filename.c_str(), &db);
    
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS SITREP ("
        "id TEXT PRIMARY KEY, ambiente TEXT, figura TEXT, estado TEXT, detalle TEXT, timestamp INTEGER);"
        "CREATE TABLE IF NOT EXISTS TRACKS ("
        "id TEXT PRIMARY KEY, ambiente TEXT, figura TEXT, estado TEXT, detalle TEXT, lat REAL, lon REAL, timestamp INTEGER);";
    sqlite3_exec(db, sql, 0, 0, 0);
}

void execute_sql(const std::string& query) {
    std::lock_guard<std::mutex> lock(db_mutex);
    char* err = 0;
    if (sqlite3_exec(db, query.c_str(), 0, 0, &err) != SQLITE_OK) {
        std::cerr << "DB Error: " << err << "\n";
        sqlite3_free(err);
    }
}

// --- LÓGICA DDS: RECEPTORES ---

// Clase base para evitar repetir código boilerplate en cada listener
class BaseListener : public virtual OpenDDS::DCPS::LocalObject<DDS::DataReaderListener> {
public:
    virtual void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) override {}
    virtual void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) override {}
    virtual void on_sample_rejected(DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) override {}
    virtual void on_liveliness_changed(DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) override {}
    virtual void on_subscription_matched(DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) override {}
    virtual void on_sample_lost(DDS::DataReader_ptr, const DDS::SampleLostStatus&) override {}
};

class DBChangeListener : public BaseListener {
public:
    void on_data_available(DDS::DataReader_ptr reader) override {
        Replication::DBChangeDataReader_var dr = Replication::DBChangeDataReader::_narrow(reader);
        Replication::DBChange msg;
        DDS::SampleInfo info;
        while (dr->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data && std::string(msg.node_id.in()) != my_node_id) {
                std::cout << "[DDS] Aplicando cambio remoto de " << msg.node_id.in() << " a " << msg.table_name.in() << "\n";
                // Lógica de parseo JSON e inserción en SQLite
            }
        }
    }
};

class HeartbeatListener : public BaseListener {
public:
    void on_data_available(DDS::DataReader_ptr reader) override {
        Replication::NodeHeartbeatDataReader_var dr = Replication::NodeHeartbeatDataReader::_narrow(reader);
        Replication::NodeHeartbeat msg;
        DDS::SampleInfo info;
        while (dr->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                std::lock_guard<std::mutex> lock(peers_mutex);
                peers[msg.node_id.in()] = {msg.uptime_ms, std::chrono::steady_clock::now()};
            }
        }
    }
};

class SnapshotRequestListener : public BaseListener {
public:
    void on_data_available(DDS::DataReader_ptr reader) override {
        Replication::SnapshotRequestDataReader_var dr = Replication::SnapshotRequestDataReader::_narrow(reader);
        Replication::SnapshotRequest msg;
        DDS::SampleInfo info;
        while (dr->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data && is_primary) {
                std::cout << "[Primary] Petición de snapshot recibida de: " << msg.requester_node_id.in() << "\n";
            }
        }
    }
};


// --- HILOS (THREADS) ---
void udp_listener_thread() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(7402);
    
    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    char buffer[2048];

    std::cout << "[UDP] Escuchando en puerto 7402...\n";
    while (true) {
        int n = recvfrom(sockfd, buffer, 2048, 0, nullptr, nullptr);
        buffer[n] = '\0';
        
        try {
            auto j = json::parse(buffer);
            std::string type = j["type"];
            std::string id = j["id"];
            
            // 1. Guardar en SQLite local
            std::string sql;
            if (type == "TRACK") {
                sql = "INSERT OR REPLACE INTO TRACKS (id, ambiente, figura, estado, detalle, lat, lon, timestamp) VALUES ('" +
                      id + "', '" + std::string(j["ambiente"]) + "', '" + std::string(j["figura"]) + "', '" +
                      std::string(j["estado"]) + "', '" + std::string(j["detalle"]) + "', " +
                      std::to_string((double)j["lat"]) + ", " + std::to_string((double)j["lon"]) + ", " +
                      std::to_string(get_current_time_ms()) + ");";
            } else if (type == "SITREP") {
                // Lógica similar para SITREP
            }
            execute_sql(sql);
            std::cout << "[UDP] Dato recibido y guardado localmente: " << id << "\n";

            // 2. Publicar vía DDS a otros nodos
            Replication::DBChange change;
            change.node_id = my_node_id.c_str();
            change.timestamp = get_current_time_ms();
            change.table_name = type == "TRACK" ? "TRACKS" : "SITREP";
            change.operation = "UPSERT";
            change.primary_key = id.c_str();
            change.data_json = j.dump().c_str();
            
            if (dbchange_dw) dbchange_dw->write(change, DDS::HANDLE_NIL);

        } catch (const std::exception& e) {
            std::cerr << "[UDP] Error parseando JSON: " << e.what() << "\n";
        }
    }
}

void primary_election_thread(Replication::NodeHeartbeatDataWriter_var hb_dw) {
    while (true) {
        // 1. Publicar Heartbeat propio
        Replication::NodeHeartbeat hb;
        hb.node_id = my_node_id.c_str();
        hb.uptime_ms = get_uptime_ms();
        hb.timestamp = get_current_time_ms();
        hb_dw->write(hb, DDS::HANDLE_NIL);

        // 2. Revisar pares caídos y calcular Primary
        {
            std::lock_guard<std::mutex> lock(peers_mutex);
            auto now = std::chrono::steady_clock::now();
            uint64_t max_uptime = hb.uptime_ms;
            std::string potential_primary = my_node_id;

            for (auto it = peers.begin(); it != peers.end(); ) {
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_seen).count();
                if (duration > 15) {
                    std::cout << "[Red] Nodo caído: " << it->first << "\n";
                    it = peers.erase(it);
                } else {
                    if (it->second.uptime_ms > max_uptime) {
                        max_uptime = it->second.uptime_ms;
                        potential_primary = it->first;
                    }
                    ++it;
                }
            }

            if (potential_primary == my_node_id && !is_primary) {
                is_primary = true;
                std::cout << ">>> [MASTER] Ahora soy el nodo Primary <<<\n";
            } else if (potential_primary != my_node_id && is_primary) {
                is_primary = false;
                std::cout << ">>> [SLAVE] He dejado de ser el Primary. El primary es " << potential_primary << " <<<\n";
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// --- MAIN ---
int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Uso: " << argv[0] << " -DCPSConfigFile rtps.ini <node_id>\n";
        return 1;
    }
    my_node_id = argv[argc - 1]; // Último argumento es el ID
    start_time_ms = get_current_time_ms();

    init_db();

    try {
        // Inicializar OpenDDS
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        DDS::DomainParticipant_var participant = dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

	if (!participant) {
            std::cerr << "ERROR FATAL: No se pudo crear el DomainParticipant.\n"
                      << "Verifica que la ruta al archivo rtps.ini sea correcta y su sintaxis válida.\n";
            return 1;
        } 

        // Registrar Tipos
        Replication::DBChangeTypeSupport_var ts_dbchange = new Replication::DBChangeTypeSupportImpl();
        ts_dbchange->register_type(participant, "");
        CORBA::String_var dbchange_type = ts_dbchange->get_type_name();
        
        Replication::NodeHeartbeatTypeSupport_var ts_hb = new Replication::NodeHeartbeatTypeSupportImpl();
        ts_hb->register_type(participant, "");
        CORBA::String_var hb_type = ts_hb->get_type_name();

        // Crear Tópicos (Configurando QoS TRANSIENT_LOCAL y KEEP_LAST_5000)
        DDS::TopicQos topic_qos;
        participant->get_default_topic_qos(topic_qos);
        topic_qos.durability.kind = DDS::TRANSIENT_LOCAL_DURABILITY_QOS;
        topic_qos.history.kind = DDS::KEEP_LAST_HISTORY_QOS;
        topic_qos.history.depth = 5000;
        topic_qos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;

        DDS::Topic_var topic_dbchange = participant->create_topic("DBChanges", dbchange_type, topic_qos, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::Topic_var topic_hb = participant->create_topic("NodeHeartbeat", hb_type, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // Publishers y Subscribers
        DDS::Publisher_var pub = participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::Subscriber_var sub = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // DataWriters
        DDS::DataWriterQos dw_qos;
        pub->get_default_datawriter_qos(dw_qos);
        dw_qos.durability.kind = DDS::TRANSIENT_LOCAL_DURABILITY_QOS;
        dw_qos.history.kind = DDS::KEEP_LAST_HISTORY_QOS;
        dw_qos.history.depth = 5000;
        dw_qos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;

        DDS::DataWriter_var dw_base = pub->create_datawriter(topic_dbchange, dw_qos, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        dbchange_dw = Replication::DBChangeDataWriter::_narrow(dw_base);

        DDS::DataWriter_var hb_base = pub->create_datawriter(topic_hb, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        Replication::NodeHeartbeatDataWriter_var hb_dw = Replication::NodeHeartbeatDataWriter::_narrow(hb_base);

        // DataReaders (con Listeners)
        DDS::DataReaderListener_var db_listener(new DBChangeListener);
        sub->create_datareader(topic_dbchange, DATAREADER_QOS_DEFAULT, db_listener, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::DataReaderListener_var hb_listener(new HeartbeatListener);
        sub->create_datareader(topic_hb, DATAREADER_QOS_DEFAULT, hb_listener, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        std::cout << "Nodo " << my_node_id << " iniciado con éxito. PID: " << getpid() << "\n";

        // Iniciar hilos
        std::thread udp_thread(udp_listener_thread);
        std::thread election_thread(primary_election_thread, hb_dw);

        // Mantener vivo (o esperar a interrupción)
        udp_thread.join();
        election_thread.join();

        participant->delete_contained_entities();
        dpf->delete_participant(participant);
        TheServiceParticipant->shutdown();

    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Exception caught in main():");
        return 1;
    }

    sqlite3_close(db);
    return 0;
}
