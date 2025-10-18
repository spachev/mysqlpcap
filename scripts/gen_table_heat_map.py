import argparse
import sys
import os

# --- Core Logic Functions ---

def parse_log_data(file_path):
    """
    Reads the performance log file (expected format: TIMESTAMP,,table_name,query_type,...)
    and extracts structured data for each metric. Handles multiple lines if present.
    """
    all_parsed_data = []
    try:
        with open(file_path, 'r') as f:
            for i, line in enumerate(f):
                line = line.strip()
                if not line:
                    continue

                # 1. Split line by the unique separator ",,"
                # We use maxsplit=1 to ensure only the first occurrence is used.
                parts = line.split(',', 1)
                if len(parts) != 2:
                    print(f"Warning: Skipping malformed line {i+1} (no ',,' separator): '{line}'", file=sys.stderr)
                    continue

                main_timestamp = parts[0].strip()
                stats_string = parts[1].strip()

                # 2. Split the statistics string by comma
                stat_fields = [f.strip() for f in stats_string.split(',')]

                # Expected fields per record: 6 (table_name, query_type, number_of_queries, min_time, max_time, avg_time)
                CHUNK_SIZE = 6

                if len(stat_fields) % CHUNK_SIZE != 0:
                     print(f"Warning: Line {i+1} has {len(stat_fields)} stats fields, which is not divisible by {CHUNK_SIZE}. Data may be truncated.", file=sys.stderr)

                # 3. Iterate over chunks and create records
                # We stop before the end if the last chunk is incomplete
                for j in range(0, len(stat_fields) - (CHUNK_SIZE - 1), CHUNK_SIZE):
                    chunk = stat_fields[j:j + CHUNK_SIZE]

                    record = {
                        'timestamp': main_timestamp,
                        'table_name': chunk[0],
                        'query_type': chunk[1],
                        'num_queries': chunk[2],
                        'min_time': chunk[3],
                        'max_time': chunk[4],
                        'avg_time': chunk[5],
                    }
                    record['total_time'] = float(record['avg_time']) * int(record['num_queries']);
                    all_parsed_data.append(record)

        return all_parsed_data
    except FileNotFoundError:
        raise FileNotFoundError(f"Input file not found: {file_path}")
    except Exception as e:
        raise Exception(f"Error reading or parsing log file: {e}")


def generate_html_content(template_content, parsed_data):
    """
    Injects the parsed data into the HTML template placeholders for the
    dropdown (using table names) and the data table (with 6 new fields).
    """
    dropdown_options = ""
    query_type_options = ""
    data_table_rows = ""

    # 1. Build the dropdown options (using unique table names for selection)
    # We use a set to ensure unique table names
    unique_ts = sorted(list(set(item['timestamp'] for item in parsed_data)))
    for ts in unique_ts:
        # The option value is the table name, used for filtering in JS
        dropdown_options += f'<option value="{ts}">{ts}</option>\n'

    unique_query_types = sorted(list(set(item['query_type'] for item in parsed_data)))
    for query_type in unique_query_types:
        # The option value is the table name, used for filtering in JS
        query_type_options += f'<option value="{query_type}">{query_type}</option>\n'

    # 2. Build the data table rows
    for item in parsed_data:
        # Table rows for visualization. data-table attribute is used by JS for filtering.
        data_table_rows += f"""
        <tr class="hover:bg-gray-50 transition duration-150 table-row-data" data-timestamp="{item['timestamp']}" data-table="{item['table_name']}">
            <td class="px-3 py-3 whitespace-nowrap text-xs font-medium text-gray-900">{item['timestamp']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-indigo-700 font-semibold">{item['table_name']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-gray-500">{item['query_type']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-gray-500">{item['num_queries']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-green-600">{item['min_time']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-red-600">{item['max_time']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-yellow-600">{item['avg_time']}</td>
            <td class="px-3 py-3 whitespace-nowrap text-sm text-purple-600">{item['total_time']}</td>
        </tr>
        """

    # Inject content into the template
    content = template_content.replace("<!-- QUERY_TYPE_OPTIONS_PLACEHOLDER -->",
                                       query_type_options)
    content = content.replace("<!-- DROPDOWN_OPTIONS_PLACEHOLDER -->", dropdown_options)
    content = content.replace("<!-- DATA_TABLE_ROWS_PLACEHOLDER -->", data_table_rows)

    return content

# --- Argument Parsing and Main Execution ---

def main():
    # Initialize the parser
    parser = argparse.ArgumentParser(
        description="Generates an HTML report from performance log data using a specified template."
    )

    # Arguments: --table-stats-file, --html-template-file, and --html-output-file
    parser.add_argument(
        '--table-stats-file',
        type=str,
        required=True,
        dest='table_stats_file',
        help='The path to the input performance log file (multi-line data).'
    )
    parser.add_argument(
        '--html-template-file',
        type=str,
        required=True,
        dest='html_template_file',
        help='The path to the HTML template file.'
    )
    parser.add_argument(
        '--html-output-file',
        type=str,
        required=True,
        dest='html_output_file',
        help='The path where the final generated HTML report will be saved.'
    )

    args = parser.parse_args()

    # Store arguments in corresponding variables
    table_stats_file = args.table_stats_file
    html_template_file = args.html_template_file
    html_output_file = args.html_output_file

    print("--- Starting Report Generation ---")

    try:
        # 1. Parse the input log data (now supporting multiple lines)
        parsed_data = parse_log_data(table_stats_file)
        print(f"Successfully parsed {len(parsed_data)} data points from {table_stats_file}.")
        if not parsed_data:
            print("Warning: Parsed data is empty. Output HTML will be minimal.", file=sys.stderr)

        # 2. Read the HTML template
        with open(html_template_file, 'r', encoding='utf-8') as f:
            template_content = f.read()
        print(f"Successfully loaded template from {html_template_file}.")

        # 3. Generate the final HTML content
        final_html = generate_html_content(template_content, parsed_data)

        # 4. Write the output HTML file
        with open(html_output_file, 'w', encoding='utf-8') as f:
            f.write(final_html)

        print(f"Success! HTML report saved to '{html_output_file}'")

    except FileNotFoundError as e:
        print(f"FATAL ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
