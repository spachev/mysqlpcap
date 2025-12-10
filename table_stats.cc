#include <iostream>
#include <vector>
#include <sstream>
#include <cctype>
#include <map>
#include <cstring>
#include <ctime>
#include "table_stats.h"
#include "sql_parser.hh"
#include "common.h"

#ifndef TEST_TABLE_STATS
extern param_info info;
#else
param_info info;
#endif

class Table_parser: public SQL_Parser
{
protected:
    Table_stats* table_stats;
    double exec_time;
public:
    Table_parser(Table_stats* table_stats, double exec_time):table_stats(table_stats),exec_time(exec_time)
    {
    }
    
    virtual ~Table_parser()
    {
    }
    
    void handle_table(const char* query_type, const char* table_name)
    {
        table_stats->update_table(table_name, query_type, exec_time);
    }
    
};

// Function to normalize and split the query into tokens
static std::vector<std::string> tokenize_query(const std::string& query)
{
    std::vector<std::string> tokens;
    std::string word;
    std::stringstream ss(query);
    
    while (ss >> word)
    {
        std::string lower_word;
        for (char c : word)
        {
            lower_word += tolower(static_cast<unsigned char>(c));
        }
        
        if (!lower_word.empty() && lower_word.back() == ';')
        {
            lower_word.pop_back();
        }
        tokens.push_back(lower_word);
    }
    return tokens;
}

void Table_query_entry::print(FILE* fp)
{
    fprintf(fp, ",%lu,%.5f,%.5f,%.5f", n, min_time, max_time, total_time / n);
}

void Table_query_info::print(FILE* fp, const char* table_name)
{
    for (auto it = entries.begin(); it != entries.end(); it++)
    {
        fprintf(fp, ",%s,%s", table_name, it->first.c_str());
        it->second.print(fp);
    }
}

void Table_stats::print(FILE* fp)
{
    if (!fp)
        return;

    std::time_t now = std::time(nullptr);
    char timestamp[20];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    fprintf(fp, "%s", timestamp);

    for (auto it = stats.begin(); it != stats.end(); ++it)
    {
        it->second.print(fp, it->first.c_str());
    }

    fprintf(fp, "\n");
}

static bool table_char(char c)
{
    return isalnum(c) || c == '_' || c == '$';
}

void Table_query_entry::update(double exec_time)
{
    n++;

    if (exec_time > max_time)
        max_time = exec_time;
    if (exec_time < min_time)
        min_time = exec_time;

    total_time += exec_time;
}

void Table_query_info::register_query(const char* type, double exec_time)
{
    auto it = entries.find(type);

    if (it == entries.end())
    {
        Table_query_entry& e = entries[type] = Table_query_entry();
        e.n = 1;
        e.min_time = e.max_time = e.total_time = exec_time;
        return;
    }

    it->second.update(exec_time);
}

void Table_stats::update_table(const char* table_token, const char* type, double exec_time)
{
    std::string table_name;
    const char* p = table_token;

    for (; *p; p++)
        if (isalpha(*p))
            break;

    for (; *p; p++)
    {
        if (!table_char(*p))
            break;

        table_name += *p;
    }

    auto it = stats.find(table_name);

    if (it == stats.end())
    {
        Table_query_info& e = stats[table_name] = Table_query_info();
        e.register_query(type, exec_time);
        return;
    }

    it->second.register_query(type, exec_time);
}

unsigned int str_sum(const char* s, size_t len)
{
    unsigned int sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += s[i];
    return sum;
}

void Table_stats::update_from_query(const char* query, size_t query_len, double exec_time)
{
    if (!query_len)
        query_len = strlen(query);

#ifdef USE_SIMPLE_PARSER
    
    std::vector<std::string> tokens = tokenize_query(std::string(query, query_len));

    if (tokens.empty())
    {
        return;
    }

    const std::string& first_token = tokens[0];
    
    // Logic to determine query type and extract tables
    if (first_token == "insert" && tokens.size() > 2 && tokens[1] == "into")
    {
        update_table(tokens[2].c_str(), first_token.c_str(), exec_time);
        return;
    }
    
    if (first_token == "update" && tokens.size() > 1)
    {
        update_table(tokens[1].c_str(), first_token.c_str(), exec_time);
        return;
    }
    
    if (first_token == "delete" && tokens.size() > 2 && tokens[1] == "from")
    {
        update_table(tokens[2].c_str(), first_token.c_str(), exec_time);
        return;
    }

    if (first_token == "select")
    {
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const std::string& token = tokens[i];
            
            if (token == "from" || token == "join")
            {
                size_t j = i + 1;
                while (j < tokens.size())
                {
                    std::string table_name = tokens[j];
                    
                    if (table_name == "where" || table_name == "group" || table_name == "order" || table_name == "on" ||
                        table_name == "inner" || table_name == "left" || table_name == "natural" || table_name == "right")
                    {
                        break;
                    }
                    
                    update_table(table_name.c_str(), first_token.c_str(), exec_time);
                    j++;
                    
                    if (j < tokens.size() && tokens[j] == "as")
                    {
                        j += 2;
                    }
                    
                    if (j < tokens.size() && (tokens[j] == "join" || tokens[j] == "on" || tokens[j] == "where" || tokens[j] == "group" || tokens[j] == "order"))
                    {
                        j = tokens.size();
                    }
                }
            }
        }
    }
#else
    Table_parser p(this, exec_time);
    if (yyparse_string(&p, query, query_len))
        fprintf(stderr, "Error parsing: %.*s\n", (int)query_len, query);
    else if (info.verbose)
        fprintf(stderr, "Success parsing: %.*s\n", (int)query_len, query);
#endif
}

#ifdef TEST_TABLE_STATS

#ifdef DEBUG_BISON
extern int yydebug;
#endif

int main() {
    Table_stats s;
    info.verbose = true;
#ifdef DEBUG_BISON
    yydebug = 1;
#endif
    const char* queries[] = {
            "SELECT * FROM employees;",
            "SELECT * FROM d1.employees;",
            "SELECT u.name FROM users AS u, posts p WHERE u.id = p.user_id;",
            "SELECT u.name FROM users u0, posts p0 WHERE u.id = p.user_id;",
            "INSERT INTO new_users (name) VALUES ('John');",
            "UPDATE products SET price = 15.00 WHERE id = 10;",
            "DELETE FROM old_logs WHERE date < '2023-01-01';",
            "SELECT count(*) FROM employees;",
            "SELECT * FROM table1 JOIN table2 ON table1.id = table2.id;",
            "SELECT * FROM (table1) JOIN table2 ON table1.id = table2.id;",
            "SELECT * FROM `table1` JOIN table2 ON table1.id = table2.id;",
            "SELECT * FROM table_self_join t1 JOIN table_self_join t2 USING (id);",
            "SELECT t.c1 FROM (SELECT id AS c1 FROM users) AS t WHERE t.c1 = 1;",
            "SELECT name FROM users WHERE id = (SELECT max(id) FROM users);",
            "SELECT id, name FROM users WHERE NOT id = 10 ORDER BY name DESC, id LIMIT 5;",
            "SELECT dept, count(*) AS num FROM employees GROUP BY dept;",
            "SELECT product_id FROM sales WHERE quantity != 0;",
            "SELECT * FROM items WHERE price <> 100.00;",
            "SELECT name FROM users WHERE id = (SELECT max(id) FROM users) GROUP BY name ORDER BY name;",
            "SELECT * FROM large_table LIMIT 100;",
            "SELECT * FROM orders WHERE (amount > 100 AND status = 'pending') OR status = 'shipped';",
            "SELECT * FROM users WHERE age <= 25 AND NOT (city = 'NY' OR city = 'LA');",
            "select c from t1 where n='1'",
            "SELECT @@max_allowed_packet,@@system_time_zone,@@time_zone,@@auto_increment_increment",
            "select 1",
            "select 1 from dual",
            "select c from dual",
            "select c from t1",
            "select c from t1 where n=1",
            "show character set",
            "show character set; /* comment */",
            "select connection_id()",
            "SELECT `storage_alias` FROM `sys_storage_table_alias` WHERE `table_name` = 'sys_script_ajax'",
            "SELECT `storage_alias` FROM `sys_storage_table_alias` WHERE `table_name` = 'sys_script_ajax' /* simeverglades001 */ ",
            "select cast(a as signed integer) from t1",
            "select convert(a, unsigned integer) from t1",
            "select IF(a like '%unsigned%', 4,4) from t1",
            "select IF(a in ('VIRTUAL', 'PERSISTENT', 'VIRTUAL GENERATED', 'STORED GENERATED') ,'YES','NO') from t1",
            "select case a when 1 then 2 when 2 then if(a>3,3,a) else 100 end from t1",
            
            "SELECT sys_dictionary_override0.`calculation`, sys_dictionary_override0.`read_only_override`, sys_metadata0.`sys_replace_on_upgrade`, sys_metadata0.`sys_updated_on`, sys_dictionary_override0.`mandatory`, sys_metadata0.`sys_class_name`, sys_dictionary_override0.`default_value_override`, sys_dictionary_override0.`display_override`, sys_metadata0.`sys_id`, sys_metadata0.`sys_updated_by`, sys_dictionary_override0.`base_table`, sys_dictionary_override0.`calculation_override`, sys_dictionary_override0.`read_only`, sys_metadata0.`sys_created_on`, sys_metadata0.`sys_name`, sys_metadata0.`sys_scope`, sys_dictionary_override0.`dependent`, sys_metadata0.`sys_created_by`, sys_dictionary_override0.`element`, sys_dictionary_override0.`mandatory_override`, sys_dictionary_override0.`attributes_override`, sys_metadata0.`sys_mod_count`, sys_dictionary_override0.`default_value`, sys_dictionary_override0.`dependent_override`, sys_metadata0.`sys_package`, sys_metadata0.`sys_update_name`, sys_dictionary_override0.`reference_qual_override`, sys_dictionary_override0.`name`, sys_dictionary_override0.`attributes`, sys_dictionary_override0.`reference_qual`, sys_metadata0.`sys_customer_update`, sys_metadata0.`sys_policy` FROM (sys_dictionary_override sys_dictionary_override0  INNER JOIN sys_metadata sys_metadata0 ON sys_dictionary_override0.`sys_id` = sys_metadata0.`sys_id` )  WHERE sys_dictionary_override0.`base_table` = 'sys_script_ajax' /* simeverglades001 */",
            "select * from information_schema.columns",
            
            "SELECT TABLE_SCHEMA TABLE_CAT, NULL TABLE_SCHEM, TABLE_NAME, COLUMN_NAME, CASE data_type WHEN 'bit' THEN -7 WHEN 'tinyblob' THEN -3 WHEN 'mediumblob' THEN -4 WHEN 'longblob' THEN -4 WHEN 'blob' THEN -4 WHEN 'tinytext' THEN 12 WHEN 'mediumtext' THEN -1 WHEN 'longtext' THEN -1 WHEN 'text' THEN -1 WHEN 'date' THEN 91 WHEN 'datetime' THEN 93 WHEN 'decimal' THEN 3 WHEN 'double' THEN 8 WHEN 'enum' THEN 12 WHEN 'float' THEN 7 WHEN 'int' THEN IF( COLUMN_TYPE like '%unsigned%', 4,4) WHEN 'bigint' THEN -5 WHEN 'mediumint' THEN 4 WHEN 'null' THEN 0 WHEN 'set' THEN 12 WHEN 'smallint' THEN IF( COLUMN_TYPE like '%unsigned%', 5,5) WHEN 'varchar' THEN 12 WHEN 'varbinary' THEN -3 WHEN 'char' THEN 1 WHEN 'binary' THEN -2 WHEN 'time' THEN 92 WHEN 'timestamp' THEN 93 WHEN 'tinyint' THEN IF(COLUMN_TYPE like 'tinyint(1)%',-7,-6) WHEN 'year' THEN 91 ELSE 1111 END DATA_TYPE, IF(COLUMN_TYPE like 'tinyint(1)%', 'BIT', UCASE(IF( COLUMN_TYPE LIKE '%(%)%', CONCAT(SUBSTRING( COLUMN_TYPE,1, LOCATE('(',COLUMN_TYPE) - 1 ), SUBSTRING(COLUMN_TYPE ,1+locate(')', COLUMN_TYPE))), COLUMN_TYPE))) TYPE_NAME, CASE DATA_TYPE WHEN 'time' THEN IF(DATETIME_PRECISION = 0, 10, CAST(11 + DATETIME_PRECISION as signed integer)) WHEN 'date' THEN 10 WHEN 'datetime' THEN IF(DATETIME_PRECISION = 0, 19, CAST(20 + DATETIME_PRECISION as signed integer)) WHEN 'timestamp' THEN IF(DATETIME_PRECISION = 0, 19, CAST(20 + DATETIME_PRECISION as signed integer)) ELSE IF(NUMERIC_PRECISION IS NULL, LEAST(CHARACTER_MAXIMUM_LENGTH,2147483647), NUMERIC_PRECISION) END COLUMN_SIZE, 65535 BUFFER_LENGTH, CONVERT (CASE DATA_TYPE WHEN 'year' THEN NUMERIC_SCALE WHEN 'tinyint' THEN 0 ELSE NUMERIC_SCALE END, UNSIGNED INTEGER) DECIMAL_DIGITS, 10 NUM_PREC_RADIX, IF(IS_NULLABLE = 'yes',1,0) NULLABLE,COLUMN_COMMENT REMARKS, COLUMN_DEFAULT COLUMN_DEF, 0 SQL_DATA_TYPE, 0 SQL_DATETIME_SUB, LEAST(CHARACTER_OCTET_LENGTH,2147483647) CHAR_OCTET_LENGTH, ORDINAL_POSITION, IS_NULLABLE, NULL SCOPE_CATALOG, NULL SCOPE_SCHEMA, NULL SCOPE_TABLE, NULL SOURCE_DATA_TYPE, IF(EXTRA = 'auto_increment','YES','NO') IS_AUTOINCREMENT, IF(EXTRA in ('VIRTUAL', 'PERSISTENT', 'VIRTUAL GENERATED', 'STORED GENERATED') ,'YES','NO') IS_GENERATEDCOLUMN FROM INFORMATION_SCHEMA.COLUMNS WHERE (TABLE_SCHEMA = 'hi02') AND (TABLE_NAME LIKE 'sys\\_script\\_ajax') AND (COLUMN_NAME LIKE '%') ORDER BY TABLE_CAT, TABLE_SCHEM, TABLE_NAME, ORDINAL_POSITION",
            "SELECT sys_amb_message00030.`to_user`, sys_amb_message00030.`channel`, sys_amb_message00030.`sys_mod_count`, sys_amb_message00030.`serialized_cometd_message`, sys_amb_message00030.`sys_updated_on`, sys_amb_message00030.`from_user`, sys_amb_message00030.`number`, sys_amb_message00030.`sys_id`, sys_amb_message00030.`sys_updated_by`, sys_amb_message00030.`sys_created_on`, sys_amb_message00030.`from_node`, sys_amb_message00030.`sys_created_by` FROM sys_amb_message0003 sys_amb_message00030  WHERE sys_amb_message00030.`number` > 512 ORDER BY sys_amb_message00030.`number` /* aztechdev026, hash:-1890806036 */",
            "select * from t1 order by t1.`number`",
            "select * from t1 order by t1.a",
            "select * from t1 order by a",
            "SELECT cmdb_par10.`attested_date`, cmdb_par20.`u_build_user`, cmdb_par10.`a_ref_14` AS `u_server_standby_data_centre`, cmdb0.`a_dtm_1` AS `u_sima_bs_activation_date`, cmdb_par10.`a_ref_12` AS `cpu_manufacturer`, cmdb0.`sys_updated_on`, cmdb0.`u_legacy_material_number`, cmdb0.`discovery_source`, cmdb0.`due_in`, cmdb_par10.`a_ref_13` AS `u_server_primary_data_centre`, cmdb_par10.`a_str_19` AS `metric_type`, cmdb0.`a_str_13` AS `used_for`, cmdb0.`gl_account`, cmdb0.`sys_created_by`, cmdb0.`a_int_20` AS `ram`, cmdb0.`a_bln_11` AS `u_is_appliance`, cmdb_par10.`a_int_27` AS `u_reminder_email_count`, cmdb0.`a_str_1` AS `u_pci_compliant`, cmdb0.`a_num_1` AS `cpu_speed`, cmdb_par10.`a_ref_59` AS `kernel_release`, cmdb0.`a_str_25` AS `u_node_o7g_system_release`, cmdb0.`a_num_5` AS `disk_space`, cmdb0.`a_ref_8` AS `processor`, cmdb_par10.`a_str_10` AS `u_os_architecture`, cmdb0.`maintenance_schedule`, cmdb0.`cost_center`, cmdb_par10.`attested_by`, cmdb0.`dns_domain`, cmdb0.`assigned`, cmdb_par20.`u_local_oe_cost_center`, cmdb_par20.`life_cycle_stage`, cmdb0.`a_int_16` AS `cd_speed`, cmdb_par10.`u_service_level`, cmdb0.`a_str_40` AS `u_dataguard_node_type`, cmdb0.`managed_by`, cmdb0.`last_discovered`, cmdb_par10.`a_str_79` AS `u_ud_identifier`, cmdb0.`u_organisational_entity`, cmdb0.`a_int_15` AS `cpu_count`, cmdb_par10.`a_str_35` AS `u_node_o7g_system_inst_type`, cmdb0.`vendor`, cmdb_par20.`life_cycle_stage_status`, cmdb0.`a_int_1` AS `x_alts_billing_discount`, cmdb_par10.`a_bln_12` AS `u_pending_for_cdb_update`, cmdb0.`u_provider`, cmdb_par10.`u_disp_name`, cmdb_par10.`a_bln_9` AS `cd_rom`, cmdb_par10.`a_str_91` AS `u_storage_type`, cmdb0.`u_managed_by_group`, cmdb_par20.`u_backup_retention_period`, cmdb0.`u_datacenter_location`, cmdb0.`correlation_id`, cmdb0.`unverified`, cmdb0.`a_str_16` AS `u_management_console`, cmdb0.`a_str_99` AS `u_ibr_dr_ip_address`, cmdb0.`a_str_3` AS `u_node_o7g_system_i10n_type`, cmdb0.`a_str_31` AS `form_factor`, cmdb0.`a_str_65` AS `u_designated_edr_host`, cmdb0.`a_str_7` AS `u_operating_system_version`, cmdb0.`a_ref_4` AS `most_frequent_user`, cmdb_par20.`u_locale`, cmdb0.`a_str_37` AS `u_os_sysconf_level`, cmdb0.`sys_created_on`, cmdb_par20.`u_drc_class`, cmdb0.`a_str_44` AS `cpu_type`, cmdb0.`u_account_aztech_oe`, cmdb0.`u_server_group_name`, cmdb0.`install_date`, cmdb0.`a_str_2` AS `hardware_substatus`, cmdb0.`fqdn`, cmdb0.`a_dtm_3` AS `u_recert_email_sent`, cmdb_par10.`a_bln_29` AS `internet_facing`, cmdb0.`a_str_89` AS `u_service_id`, cmdb_par20.`u_itom_updated_by`, cmdb_par20.`u_dr_replication_method`, cmdb_par10.`a_bln_16` AS `u_pending_for_approval`, cmdb0.`a_str_6` AS `hardware_status`, cmdb0.`name`, cmdb0.`subcategory`, cmdb_par20.`u_rack_slot`, cmdb0.`a_str_86` AS `u_restriction`, cmdb0.`a_str_5` AS `default_gateway`, cmdb0.`a_bln_1` AS `virtual`, cmdb0.`a_str_43` AS `chassis_type`, cmdb_par20.`a_str_121` AS `u_ibm_alias`, cmdb0.`assignment_group`, cmdb_par10.`u_service_name_desired`, cmdb_par10.`a_ref_9` AS `u_os_distribution`, cmdb_par10.`managed_by_group`, cmdb0.`u_sla_report`, cmdb0.`a_str_28` AS `u_oracle_asm_lun_size`, cmdb_par10.`u_app_resource_jndi_name`, cmdb0.`sys_id`, cmdb0.`mac_address`, cmdb_par20.`u_is_discoverable`, cmdb0.`company`, cmdb_par20.`u_retired_date`, cmdb_par10.`a_ref_11` AS `u_preempt_host`, cmdb0.`a_ref_3` AS `cluster_name`, cmdb_par20.`is_encrypted`, cmdb_par20.`cmdb_ot_entity`, cmdb_par20.`attestation_status`, cmdb0.`monitor`, cmdb0.`model_id`, cmdb0.`ip_address`, cmdb0.`a_str_49` AS `u_server_uptime`, cmdb_par10.`duplicate_of`, cmdb0.`u_organisational_entity_list`, cmdb0.`cost_cc`, cmdb_par10.`a_str_20` AS `u_ordered_system_size`, cmdb0.`schedule`, cmdb0.`order_date`, cmdb_par10.`environment`, cmdb_par10.`attested`, cmdb0.`location`, cmdb_par10.`a_ref_22` AS `u_sima_business_service`, cmdb0.`lease_id`, cmdb0.`a_bln_4` AS `u_cluster_node`, cmdb0.`a_str_41` AS `firewall_status`, cmdb0.`a_int_22` AS `os_address_width`, cmdb0.`operational_status`, cmdb_par10.`a_str_26` AS `os_service_pack`, cmdb_par10.`a_bln_13` AS `u_updated_ci`, cmdb_par10.`a_int_21` AS `cpu_core_thread`, cmdb0.`a_str_103` AS `u_network_segment_backup`, cmdb0.`first_discovered`, cmdb0.`a_str_4` AS `u_dr_ip_address`, cmdb0.`invoice_number`, cmdb0.`warranty_expiration`, cmdb_par10.`a_str_27` AS `cpu_name`, cmdb0.`owned_by`, cmdb0.`checked_out`, cmdb0.`sys_domain_path`, cmdb0.`a_str_50` AS `classification`, cmdb0.`a_str_17` AS `object_id`, cmdb_par20.`business_unit`, cmdb0.`a_str_11` AS `u_operating_system_name`, cmdb0.`u_service_name`, cmdb_par10.`u_iso_agreement`, cmdb0.`a_str_67` AS `u_mode_of_operation`, cmdb0.`purchase_date`, cmdb_par10.`u_app_resource_type`, cmdb0.`short_description`, cmdb0.`a_dte_1` AS `u_next_patch_day`, cmdb_par10.`a_str_39` AS `floppy`, cmdb0.`u_microcode`, cmdb_par10.`a_str_94` AS `u_cluster_member`, cmdb_par10.`a_str_36` AS `os_domain`, cmdb0.`u_ci_flagged_for_review`, cmdb0.`a_str_56` AS `u_operating_system_d10n`, cmdb0.`can_print`, cmdb0.`sys_class_name`, cmdb0.`manufacturer`, cmdb_par10.`a_str_93` AS `u_oracle_asm_san_type`, cmdb0.`a_str_18` AS `u_purpose_of_use`, cmdb0.`u_stage`, cmdb_par10.`a_str_92` AS `u_business_service_name`, cmdb0.`u_model`, cmdb0.`model_number`, cmdb_par20.`u_itom_updated_date`, cmdb0.`assigned_to`, cmdb0.`start_date`, cmdb_par20.`a_str_145` AS `u_domain_name`, cmdb0.`a_dtm_4` AS `u_last_confirmation_date`, cmdb0.`a_str_34` AS `os_version`, cmdb0.`serial_number`, cmdb_par10.`u_data_classification`, cmdb_par10.`a_str_15` AS `u_ip_address_list`, cmdb0.`support_group`, cmdb_par20.`u_room`, cmdb0.`a_str_54` AS `u_network_segment`, cmdb0.`a_str_8` AS `u_frame_slot`, cmdb_par20.`u_rack`, cmdb0.`attributes`, cmdb0.`a_dtm_2` AS `u_cutover_date`, cmdb0.`asset`, cmdb0.`a_int_14` AS `cpu_core_count`, cmdb0.`skip_sync`, cmdb_par20.`a_bln_50` AS `u_excluded`, cmdb_par20.`u_itom_discovery_source`, cmdb_par20.`a_str_150` AS `u_custom_roles`, cmdb_par10.`u_service_layer`, cmdb_par10.`attestation_score`, cmdb0.`a_str_63` AS `u_dataguard_type`, cmdb_par20.`u_request_id`, cmdb0.`u_admin_server`, cmdb0.`sys_updated_by`, cmdb_par10.`install_directory`, cmdb0.`sys_domain`, cmdb0.`u_tech_support_group`, cmdb_par10.`a_ref_61` AS `processor_name`, cmdb0.`a_bln_3` AS `u_paragraph_203_restriction`, cmdb0.`asset_tag`, cmdb0.`u_bs_list`, cmdb_par10.`a_ref_16` AS `dr_backup`, cmdb0.`a_str_76` AS `u_work_notes`, cmdb_par10.`u_edr_class`, cmdb_par10.`u_state`, cmdb0.`change_control`, cmdb0.`a_dte_2` AS `u_last_patch_day`, cmdb0.`delivery_date`, cmdb0.`install_status`, cmdb0.`supported_by`, cmdb0.`a_str_87` AS `u_i16e_middleware`, cmdb_par10.`a_ref_60` AS `u_sysconf_application_flag`, cmdb0.`u_external_provider_identifier`, cmdb0.`u_sima_status`, cmdb0.`a_str_14` AS `u_business_service_critical`, cmdb0.`u_service_instance`, cmdb0.`u_provider_tag`, cmdb0.`po_number`, cmdb0.`u_node_model`, cmdb_par20.`a_str_147` AS `cluster_id`, cmdb0.`sys_class_path`, cmdb0.`checked_in`, cmdb_par10.`u_ssr_requestor_group`, cmdb0.`justification`, cmdb0.`department`, cmdb_par20.`a_str_133` AS `u_pd_number`, cmdb0.`a_str_38` AS `u_oracle_licensed_option`, cmdb0.`cost`, cmdb0.`comments`, cmdb0.`a_str_61` AS `os`, cmdb0.`a_str_29` AS `u_purpose`, cmdb_par10.`u_service_state`, cmdb0.`a_str_42` AS `u_oracle_asm_storage_disk`, cmdb0.`sys_mod_count`, cmdb_par20.`u_drci`, cmdb0.`u_node_family`, cmdb_par10.`u_application`, cmdb0.`due`, cmdb_par10.`u_app_resource_port`, cmdb0.`a_bln_2` AS `u_purchased_or_leased`, cmdb0.`a_str_30` AS `u_os_maintenance_level`, cmdb0.`category`, cmdb0.`fault_count`, cmdb_par10.`a_str_24` AS `host_name` FROM ((cmdb cmdb0  INNER JOIN cmdb$par1 cmdb_par10 ON cmdb0.`sys_id` = cmdb_par10.`sys_id` )  INNER JOIN cmdb$par2 cmdb_par20 ON cmdb0.`sys_id` = cmdb_par20.`sys_id` )  WHERE cmdb0.`sys_class_path` = '/!!/!D/!!/!$/!)' AND cmdb0.`sys_id` = '88124aa51b16dc102eb0628f7b4bcbe3' /* aztechdev030, gs:glide.scheduler.worker.1, tx:a35e106c3b150e109d5a037aa5e45afd, hash:54758874 */",
            "select * from cmdb$par1 cmdb_par10 where sys_id > 'a'",
            "select * from cmdbpar1 cmdb_par10 where sys_id > 'a'",
            "SELECT task0.`sys_id` FROM task task0  WHERE task0.`sys_class_name` = 'incident' ORDER BY task0.`a_dtm_8` limit 0,1 /* aztechdev026, gs:glide.scheduler.worker.4, tx:6c93a864931d8e10d44f38edfaba10eb, hash:-361364829 */",
            "SELECT task0.`sys_id` FROM task task0  WHERE task0.`sys_class_name` = 'incident' ORDER BY task0.`a_dtm_8` limit 1 /* aztechdev026, gs:glide.scheduler.worker.4, tx:6c93a864931d8e10d44f38edfaba10eb, hash:-361364829 */",
            "(SELECT sysevent00030.`sys_id`, 'sysevent0003' AS `sys_table_name`, sysevent00030.`sys_created_on` AS `order_0` FROM sysevent0003 sysevent00030  ignore index(sys_created_on)  WHERE (sysevent00030.`state` = 'queued.3e41823347010210a828b9c5536d4346' OR sysevent00030.`state` = 'resumed.3e41823347010210a828b9c5536d4346') AND sysevent00030.`queue` = 'process_automation' ORDER BY sysevent00030.`sys_created_on` limit 0,500) UNION ALL (SELECT sysevent00040.`sys_id`, 'sysevent0004' AS `sys_table_name`, sysevent00040.`sys_created_on` AS `order_0` FROM sysevent0004 sysevent00040  ignore index(sys_created_on)  WHERE (sysevent00040.`state` = 'queued.3e41823347010210a828b9c5536d4346' OR sysevent00040.`state` = 'resumed.3e41823347010210a828b9c5536d4346') AND sysevent00040.`queue` = 'process_automation' ORDER BY sysevent00040.`sys_created_on` limit 0,500) UNION ALL (SELECT sysevent00050.`sys_id`, 'sysevent0005' AS `sys_table_name`, sysevent00050.`sys_created_on` AS `order_0` FROM sysevent0005 sysevent00050  ignore index(sys_created_on)  WHERE (sysevent00050.`state` = 'queued.3e41823347010210a828b9c5536d4346' OR sysevent00050.`state` = 'resumed.3e41823347010210a828b9c5536d4346') AND sysevent00050.`queue` = 'process_automation' ORDER BY sysevent00050.`sys_created_on` limit 0,500) UNION ALL (SELECT sysevent00060.`sys_id`, 'sysevent0006' AS `sys_table_name`, sysevent00060.`sys_created_on` AS `order_0` FROM sysevent0006 sysevent00060  ignore index(sys_created_on)  WHERE (sysevent00060.`state` = 'queued.3e41823347010210a828b9c5536d4346' OR sysevent00060.`state` = 'resumed.3e41823347010210a828b9c5536d4346') AND sysevent00060.`queue` = 'process_automation' ORDER BY sysevent00060.`sys_created_on` limit 0,500) UNION ALL (SELECT sysevent00000.`sys_id`, 'sysevent0000' AS `sys_table_name`, sysevent00000.`sys_created_on` AS `order_0` FROM sysevent0000 sysevent00000  ignore index(sys_created_on)  WHERE (sysevent00000.`state` = 'queued.3e41823347010210a828b9c5536d4346' OR sysevent00000.`state` = 'resumed.3e41823347010210a828b9c5536d4346') AND sysevent00000.`queue` = 'process_automation' ORDER BY sysevent00000.`sys_created_on` limit 0,500) UNION ALL (SELECT sysevent00010.`sys_id`, 'sysevent0001' AS `sys_table_name`, sysevent00010.`sys_created_on` AS `order_0` FROM sysevent0001 sysevent00010  ignore index(sys_created_on)  WHERE (sysevent00010.`state` = 'queued.3e41823347010210a828b9c5536d4346' OR sysevent00010.`state` = 'resumed.3e41823347010210a828b9c5536d4346') AND sysevent00010.`queue` = 'process_automation' ORDER BY sysevent00010.`sys_created_on` limit 0,500) ORDER BY `order_0` limit 0,500 /* aztechdev030, gs:glide.scheduler.worker.4, tx:1275a4ec3b950e109d5a037aa5e45a68, hash:2127595292 */",
            "(select a from t1) UNION (select b from t2)",
            "(select a from t1) UNION ALL (select b from t2)",
            "select * from t1 ignore index(a)",
            "select distinct n as dn from t1",
            "select `a` from t1",
            "SELECT DISTINCT cmdb_rel_ci0.`parent` FROM cmdb_rel_ci cmdb_rel_ci0  WHERE cmdb_rel_ci0.`type` = 'bf83653c0ab30150761028c73a4de0f4' AND cmdb_rel_ci0.`child` IN (SELECT cmdb0.`sys_id` FROM cmdb cmdb0  WHERE cmdb0.`sys_class_path` = '/!!/!$/#H/!1') AND cmdb_rel_ci0.`parent` IN (SELECT cmdb0.`sys_id` FROM cmdb cmdb0  WHERE cmdb0.`sys_class_path` = '/!!/!$/#H/!$' AND (cmdb0.`sys_class_name` = 'cmdb_ci_db_hbase_instance' AND (cmdb0.`sys_id` <= 'ffcaa9421b27d514e728a6c8bb4bcb5d' AND cmdb0.`sys_id` >= '00045b5a1b3c6554e10340cfbb4bcb42'))) /* aztechdev030, gs:glide.scheduler.worker.5, tx:a2452cac3b950e109d5a037aa5e45a48, hash:1414072522 */ ",
            "SELECT sys_amb_channel_presence0.`cometd_session_id`, sys_amb_channel_presence0.`user_name`, sys_amb_channel_presence0.`sys_mod_count`, sys_amb_channel_presence0.`sys_updated_on`, sys_amb_channel_presence0.`channel_hash`, sys_amb_channel_presence0.`sys_id`, sys_amb_channel_presence0.`event_type`, sys_amb_channel_presence0.`sys_updated_by`, sys_amb_channel_presence0.`sys_created_on`, sys_amb_channel_presence0.`glide_session_id`, sys_amb_channel_presence0.`channel_id`, sys_amb_channel_presence0.`user`, sys_amb_channel_presence0.`user_display_name`, sys_amb_channel_presence0.`user_agent`, sys_amb_channel_presence0.`sys_created_by`, sys_amb_channel_presence0.`node_id` FROM sys_amb_channel_presence sys_amb_channel_presence0  WHERE sys_amb_channel_presence0.`node_id` NOT IN ('06bc693e99633d8a114763731b1ad679' , '7038fc1e26bf8a0782ed09086cc4e6c9' , 'ffce269f070f9208c398fe0ca97c9c95' , '3ae3575903d83f5afd40196a0beca08d' , '9e05e8d384bba3df7abfe56e6e25c150' , 'a0ea5e2ea954b7fb6f53023c4bfa42e2') /* aztechdev031, hash:2105958680 */",
            "SELECT sla_async_queue0.`document_table` AS `document_table`, sla_async_queue0.`document_id` AS `document_id`, sys_trigger1.`name`, sla_async_queue0.`sys_trigger` AS `sys_trigger` FROM (sla_async_queue sla_async_queue0  LEFT JOIN sys_trigger sys_trigger1 ON sla_async_queue0.`sys_trigger` = sys_trigger1.`sys_id` )  WHERE sla_async_queue0.`state` IN ('queued' , 'processing') GROUP BY sla_async_queue0.`document_table`, sla_async_queue0.`document_id`, sys_trigger1.`name`, sla_async_queue0.`sys_trigger` ORDER BY sla_async_queue0.`document_table`,sla_async_queue0.`document_id`,sys_trigger1.`name` /* aztechdev029, gs:glide.scheduler.worker.7, tx:b975ace8ebd14a10a876fb93dad0cd99, hash:337234395 */",
            "select t1.n1 from d1.t1 group by t1.n1",
            "select t1.n1 from d1.t1 group by n1",
            "SELECT sys_trigger0.`sys_id` FROM sys_trigger sys_trigger0  WHERE sys_trigger0.`state` IN (1 , 2 , -1) AND (sys_trigger0.`claimed_by` = 'app130152.dus201.service-now.com:aztechdev026' OR sys_trigger0.`claimed_by` IS NULL ) limit 0,35 /* aztechdev026, gs:SYSTEM, tx:097528a0935d8e10d44f38edfaba1036, hash:1732200128 */ ",
            "select * from ((select a from t1) union (select a from t2))",
            "select * from t1 where exists (select * from t2 where t2.n = t1.n)",
            "select * from t1 where 1",
            "SELECT awa_agent_channel_availabilit0.`sys_id`, awa_agent_channel_availabilit0.`agent`, awa_agent_channel_availabilit0.`service_channel`, awa_agent_channel_availabilit0.`sys_updated_by`, awa_agent_channel_availabilit0.`sys_created_on`, awa_agent_channel_availabilit0.`presence_state`, awa_agent_channel_availabilit0.`available`, awa_agent_channel_availabilit0.`sys_mod_count`, awa_agent_channel_availabilit0.`sys_updated_on`, awa_agent_channel_availabilit0.`sys_created_by` FROM awa_agent_channel_availability awa_agent_channel_availabilit0  WHERE awa_agent_channel_availabilit0.`available` = 1 AND awa_agent_channel_availabilit0.`presence_state` = 'c5334dfb1bba74d46d83ba2c9b4bcbbd' AND (awa_agent_channel_availabilit0.`service_channel` = '319870371bb674d46d83ba2c9b4bcb64' AND EXISTS (SELECT awa_agent_presence0.`agent` FROM awa_agent_presence awa_agent_presence0  WHERE awa_agent_presence0.`current_presence_state` IN ('c5334dfb1bba74d46d83ba2c9b4bcbbd') AND awa_agent_presence0.`agent` = awa_agent_channel_availabilit0.`agent`)) /* aztechdev023, gs:glide.scheduler.worker.4, tx:f875eca84755c610a828b9c5536d43b2, hash:536018594 */",

        };

        const size_t num_queries = sizeof(queries) / sizeof(queries[0]);

        for (size_t i = 0; i < num_queries; ++i) {
            s.update_from_query(queries[i]);
        }
    s.print(stdout);
    return 0;
}

#endif
